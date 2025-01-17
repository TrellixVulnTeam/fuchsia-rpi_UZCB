// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONNECTIVITY_MANAGER_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONNECTIVITY_MANAGER_IMPL_H_

#include <Weave/DeviceLayer/ConnectivityManager.h>
#include <Weave/DeviceLayer/internal/GenericConnectivityManagerImpl.h>
#if WEAVE_DEVICE_CONFIG_ENABLE_WOBLE
#include <Weave/DeviceLayer/internal/GenericConnectivityManagerImpl_BLE.h>
#else
#include <Weave/DeviceLayer/internal/GenericConnectivityManagerImpl_NoBLE.h>
#endif
#include <Weave/DeviceLayer/internal/GenericConnectivityManagerImpl_NoThread.h>
#include <Weave/Profiles/network-provisioning/NetworkProvisioning.h>
#include <Weave/Profiles/weave-tunneling/WeaveTunnelCommon.h>
#include <Weave/Profiles/weave-tunneling/WeaveTunnelConnectionMgr.h>

#include <Weave/Support/FlagUtils.hpp>

namespace nl {
namespace Inet {
class IPAddress;
}  // namespace Inet
}  // namespace nl

namespace nl {
namespace Weave {
namespace DeviceLayer {

class PlatformManagerImpl;

namespace Internal {

class NetworkProvisioningServerImpl;
template <class ImplClass>
class GenericNetworkProvisioningServerImpl;

}  // namespace Internal

/**
 * Concrete implementation of the ConnectivityManager singleton object for the Fuchsia platform.
 */
class ConnectivityManagerImpl final
    : public ConnectivityManager,
      public Internal::GenericConnectivityManagerImpl<ConnectivityManagerImpl>,
#if WEAVE_DEVICE_CONFIG_ENABLE_WOBLE
      public Internal::GenericConnectivityManagerImpl_BLE<ConnectivityManagerImpl>,
#else
      public Internal::GenericConnectivityManagerImpl_NoBLE<ConnectivityManagerImpl>,
#endif
      public Internal::GenericConnectivityManagerImpl_NoThread<ConnectivityManagerImpl> {
#if WEAVE_CONFIG_ENABLE_TUNNELING
  using TunnelConnNotifyReasons =
      ::nl::Weave::Profiles::WeaveTunnel::WeaveTunnelConnectionMgr::TunnelConnNotifyReasons;
#endif

  // Allow the ConnectivityManager interface class to delegate method calls to
  // the implementation methods provided by this class.
  friend class ConnectivityManager;

 private:
  // ===== Members that implement the ConnectivityManager abstract interface.

  WiFiStationMode _GetWiFiStationMode(void);
  WEAVE_ERROR _SetWiFiStationMode(WiFiStationMode val);
  bool _IsWiFiStationEnabled(void);
  bool _IsWiFiStationApplicationControlled(void);
  bool _IsWiFiStationConnected(void);
  uint32_t _GetWiFiStationReconnectIntervalMS(void);
  WEAVE_ERROR _SetWiFiStationReconnectIntervalMS(uint32_t val);
  bool _IsWiFiStationProvisioned(void);
  void _ClearWiFiStationProvision(void);
  WiFiAPMode _GetWiFiAPMode(void);
  WEAVE_ERROR _SetWiFiAPMode(WiFiAPMode val);
  bool _IsWiFiAPActive(void);
  bool _IsWiFiAPApplicationControlled(void);
  void _DemandStartWiFiAP(void);
  void _StopOnDemandWiFiAP(void);
  void _MaintainOnDemandWiFiAP(void);
  uint32_t _GetWiFiAPIdleTimeoutMS(void);
  void _SetWiFiAPIdleTimeoutMS(uint32_t val);
  WEAVE_ERROR _GetAndLogWifiStatsCounters(void);
  bool _HaveIPv4InternetConnectivity(void);
  bool _HaveIPv6InternetConnectivity(void);
  ServiceTunnelMode _GetServiceTunnelMode(void);
  WEAVE_ERROR _SetServiceTunnelMode(ServiceTunnelMode val);
  bool _IsServiceTunnelConnected(void);
  bool _IsServiceTunnelRestricted(void);
  bool _HaveServiceConnectivityViaTunnel(void);
  bool _HaveServiceConnectivity(void);
  WEAVE_ERROR _Init(void);
  void _OnPlatformEvent(const WeaveDeviceEvent* event);
  bool _CanStartWiFiScan();
  void _OnWiFiScanDone();
  void _OnWiFiStationProvisionChange();
  static const char* _WiFiStationModeToStr(WiFiStationMode mode);
  static const char* _WiFiAPModeToStr(WiFiAPMode mode);
  static const char* _ServiceTunnelModeToStr(ServiceTunnelMode mode);

  // ===== Members for internal use by the following friends.

  friend ConnectivityManager& ConnectivityMgr(void);
  friend ConnectivityManagerImpl& ConnectivityMgrImpl(void);

  static ConnectivityManagerImpl sInstance;

  // ===== Private members reserved for use by this class only.

  enum WiFiStationState {
    kWiFiStationState_NotConnected,
    kWiFiStationState_Connecting,
    kWiFiStationState_Connecting_Succeeded,
    kWiFiStationState_Connecting_Failed,
    kWiFiStationState_Connected,
    kWiFiStationState_Disconnecting,
  };

  enum WiFiAPState {
    kWiFiAPState_NotActive,
    kWiFiAPState_Activating,
    kWiFiAPState_Active,
    kWiFiAPState_Deactivating,
  };

  enum Flags {
    kFlag_HaveIPv4InternetConnectivity = 0x0001,
    kFlag_HaveIPv6InternetConnectivity = 0x0002,
    kFlag_ServiceTunnelStarted = 0x0004,
    kFlag_ServiceTunnelUp = 0x0008,
    kFlag_AwaitingConnectivity = 0x0010,
  };

  uint64_t mLastStationConnectFailTime;
  uint64_t mLastAPDemandTime;
  WiFiStationMode mWiFiStationMode;
  WiFiStationState mWiFiStationState;
  WiFiAPMode mWiFiAPMode;
  WiFiAPState mWiFiAPState;
  ServiceTunnelMode mServiceTunnelMode;
  uint32_t mWiFiStationReconnectIntervalMS;
  uint32_t mWiFiAPIdleTimeoutMS;
  uint16_t flags_;

  // Handle service tunnel notifications.
  // |reason| specifies the reason for the notification.
  // |err| specifies if there was an error during a tunnel related operation.
  // |app_ctx| provides application context.
  static void HandleServiceTunnelNotification(
      Profiles::WeaveTunnel::WeaveTunnelConnectionMgr::TunnelConnNotifyReasons reason,
      WEAVE_ERROR err, void* app_ctx);

  // Returns a boolean to specify if the tunnel should be started.
  bool ShouldStartServiceTunnel(void);

  // Start the service tunnel.
  void StartServiceTunnel(void);

  // Stop the service tunnel.
  void StopServiceTunnel(void);
};

inline bool ConnectivityManagerImpl::_IsWiFiStationApplicationControlled(void) {
  return mWiFiStationMode == kWiFiStationMode_ApplicationControlled;
}

inline bool ConnectivityManagerImpl::_IsWiFiStationConnected(void) {
  return mWiFiStationState == kWiFiStationState_Connected;
}

inline bool ConnectivityManagerImpl::_IsWiFiAPApplicationControlled(void) {
  return mWiFiAPMode == kWiFiAPMode_ApplicationControlled;
}

inline uint32_t ConnectivityManagerImpl::_GetWiFiStationReconnectIntervalMS(void) {
  return mWiFiStationReconnectIntervalMS;
}

inline ConnectivityManager::WiFiAPMode ConnectivityManagerImpl::_GetWiFiAPMode(void) {
  return mWiFiAPMode;
}

inline bool ConnectivityManagerImpl::_IsWiFiAPActive(void) {
  return mWiFiAPState == kWiFiAPState_Active;
}

inline uint32_t ConnectivityManagerImpl::_GetWiFiAPIdleTimeoutMS(void) {
  return mWiFiAPIdleTimeoutMS;
}

inline bool ConnectivityManagerImpl::_HaveIPv4InternetConnectivity(void) {
  return ::nl::GetFlag(flags_, kFlag_HaveIPv4InternetConnectivity);
}

inline bool ConnectivityManagerImpl::_HaveIPv6InternetConnectivity(void) {
  return ::nl::GetFlag(flags_, kFlag_HaveIPv6InternetConnectivity);
}

inline ConnectivityManager::ServiceTunnelMode ConnectivityManagerImpl::_GetServiceTunnelMode(void) {
  return mServiceTunnelMode;
}

inline bool ConnectivityManagerImpl::_CanStartWiFiScan() {
  return mWiFiStationState != kWiFiStationState_Connecting;
}

inline bool ConnectivityManagerImpl::_HaveServiceConnectivity(void) {
  return HaveServiceConnectivityViaTunnel() || HaveServiceConnectivityViaThread();
}

/**
 * Returns the public interface of the ConnectivityManager singleton object.
 *
 * Weave applications should use this to access features of the ConnectivityManager object
 * that are common to all platforms.
 */
inline ConnectivityManager& ConnectivityMgr(void) { return ConnectivityManagerImpl::sInstance; }

/**
 * Returns the platform-specific implementation of the ConnectivityManager singleton object.
 *
 * Weave applications can use this to gain access to features of the ConnectivityManager
 * that are specific to the Fuchsia platform.
 */
inline ConnectivityManagerImpl& ConnectivityMgrImpl(void) {
  return ConnectivityManagerImpl::sInstance;
}

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONNECTIVITY_MANAGER_IMPL_H_
