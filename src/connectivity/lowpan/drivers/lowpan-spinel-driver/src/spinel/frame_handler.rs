// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::prelude::*;

use anyhow::Error;
use futures::channel::oneshot;
use futures::sink::Sink;
use parking_lot::Mutex;
use std::sync::Arc;

/// Implements outbound Spinel frame handling and response tracking.
///
/// Note that this type doesn't handle state tracking: that is
/// handled by the type that uses this struct.
#[derive(Debug)]
pub struct FrameHandler<S> {
    requests: RequestTracker,
    spinel_sink: futures::lock::Mutex<S>,
}

impl<S> FrameHandler<S> {
    /// Creates a new frame handler with given frame sink.
    pub fn new(spinel_sink: S) -> FrameHandler<S> {
        FrameHandler {
            requests: Default::default(),
            spinel_sink: futures::lock::Mutex::new(spinel_sink),
        }
    }

    /// Cancels all pending requests in the request tracker.
    pub fn clear(&self) {
        self.requests.clear();
    }
}

impl<S> FrameHandler<S>
where
    S: for<'a> Sink<&'a [u8], Error = anyhow::Error> + Unpin + Send,
{
    /// This method dispatches an inbound frame to the response
    /// handler associated with its TID, if any.
    ///
    /// This method is generally called whenever an inbound frame is received
    /// to ensure that responses make it to their handlers.
    pub fn handle_inbound_frame(&self, frame: SpinelFrameRef<'_>) -> Result<(), Error> {
        traceln!("FrameHandler::handle_inbound_frame: Got frame: {:?}", frame);

        if let Some(tid) = frame.header.tid() {
            if let Some(handler_mutex) = self.requests.retrieve_handler(tid) {
                traceln!("FrameHandler::handle_inbound_frame: Got handler");
                handler_mutex.lock().on_response(Ok(frame))?;
                traceln!("FrameHandler::handle_inbound_frame: Handled!");
            } else {
                Err(format_err!("No handler for TID {}", tid))?;
            }
        }

        Ok(())
    }

    /// Sends a request to the device, asynchronously returning the response.
    ///
    /// `request` can be any type that implements the [`RequestDesc`] trait.
    /// It describes not only how to form the body of the request but also
    /// how to interpret the response and what result should be returned.
    ///
    /// To cancel a request in progress, drop the returned future.
    pub async fn send_request<RD>(&self, request: RD) -> Result<RD::Result, Error>
    where
        RD: RequestDesc + 'static,
    {
        // Buffer contains an initial dummy byte for the header.
        // We will fill out the header byte at the end of this method.
        // Note that this is causing an allocation to occur for every
        // call, which is sub-optimal. If performance is problematic,
        // this should be optimized.
        let mut buffer = vec![0u8];

        traceln!("FrameHandler::send_request: building request");

        // Append the actual request to the rest of the buffer.
        request.write_request(&mut buffer)?;

        // Create a one-shot channel to handle our response.
        let (sender, receiver) = oneshot::channel();

        // Create our response handler as a closure. This is invoked whenever
        // we get a response or the transaction is cancelled.
        //
        // This works because closures with this signature have a blanket
        // implementation of the `ResponseHandler` trait.
        let handler = move |response: Result<SpinelFrameRef<'_>, Canceled>| {
            traceln!("FrameHandler::send_request: Response handler invoked with {:?}", response);
            sender.send(request.on_response(response)).map_err(|_| Error::from(Canceled))
        };

        // Simultaneously put our closure in an ArcMutex and cast it
        // as a `dyn ResponseHandler`. This will be held onto by
        // the enclosing future.
        let handler = Arc::new(Mutex::new(Some(handler))) as Arc<Mutex<dyn ResponseHandler>>;

        // Register our response handler with the request tracker,
        // giving us our TID.
        let tid = self.requests.register_handler(&handler).await;

        traceln!("FrameHandler::send_request: handler registered: tid={}", tid);

        // Now that we have a TID, set the header byte
        buffer[0] = Header::new(0, Some(tid)).expect("Invalid NLI/TID").into();

        traceln!("FrameHandler::send_request: Sending frame: {:?}", buffer);

        // Actually send our request.
        self.spinel_sink.lock().await.send(&buffer).await?;

        // Wait for the response
        receiver.await?
    }
}

#[cfg(test)]
#[allow(unused_mut)]
pub(crate) mod tests {
    use super::mock;
    use super::*;

    use fidl_fuchsia_lowpan_spinel::DeviceEvent as SpinelDeviceEvent;
    use fuchsia_async as fasync;
    use futures::future::{join, select};
    use matches::assert_matches;
    use mock::DeviceRequest;
    use std::convert::TryInto;

    #[fasync::run_until_stalled(test)]
    async fn test_spinel_frame_handler() {
        const MAX_FRAME_SIZE: u32 = 2000;
        let (device_sink, mut device_stream, mut device_event_sender, mut device_request_receiver) =
            mock::new_mock_spinel_pair(MAX_FRAME_SIZE);

        assert_matches!(device_sink.open().now_or_never(), Some(Ok(())));

        let frame_handler = FrameHandler::new(device_sink);

        let host_task = async {
            traceln!("host_task: Sending request");

            let result = frame_handler.send_request(CmdNoop).await;

            assert_matches!(result, Ok(_));
        }
        .boxed_local();

        let inbound_pump = async {
            loop {
                traceln!("inbound_pump: Waiting to receive frame");
                let frame =
                    device_stream.try_next().await.expect("error on receive frame").unwrap();
                traceln!("inbound_pump: Got frame {:?}", frame);
                let frame = SpinelFrameRef::try_unpack_from_slice(frame.as_slice());
                assert_matches!(frame.as_ref(), Ok(_));
                assert_matches!(frame_handler.handle_inbound_frame(frame.unwrap()), Ok(_));
            }
        }
        .boxed_local();

        let ncp_task = async {
            traceln!("ncp_task: Waiting for DeviceRequest::Open");
            assert_eq!(device_request_receiver.next().await, Some(DeviceRequest::Open));

            traceln!("ncp_task: Waiting for DeviceRequest::ReadyToReceiveFrames");
            assert_eq!(
                device_request_receiver.next().await,
                Some(DeviceRequest::ReadyToReceiveFrames(
                    crate::spinel::device_client::INBOUND_FRAME_WINDOW_SIZE.try_into().unwrap()
                ))
            );

            traceln!("ncp_task: Sending SpinelDeviceEvent::OnReadyForSendFrames");
            assert!(device_event_sender
                .start_send(Ok(SpinelDeviceEvent::OnReadyForSendFrames { number_of_frames: 2 }))
                .is_ok());

            traceln!("ncp_task: Waiting for DeviceRequest::SendFrame");
            let request = device_request_receiver.next().await;
            assert_eq!(request, Some(DeviceRequest::SendFrame(vec![0x81, 0x00])));

            traceln!("ncp_task: Sending SpinelDeviceEvent::OnReceiveFrame");
            assert_matches!(
                device_event_sender
                    .start_send(Ok(SpinelDeviceEvent::OnReceiveFrame { data: vec![0x81, 00] })),
                Ok(_)
            );
        }
        .boxed_local();

        let host_task = select(inbound_pump, host_task);

        let _ = join(host_task, ncp_task).await;
    }
}
