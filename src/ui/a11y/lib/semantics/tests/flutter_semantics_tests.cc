// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/ui/base_view/embedded_view_utils.h"
#include "src/ui/a11y/lib/semantics/tests/semantics_integration_test_fixture.h"
#include "src/ui/testing/views/embedder_view.h"

namespace accessibility_test {
namespace {

constexpr char kClientUrl[] = "fuchsia-pkg://fuchsia.com/a11y-demo#meta/a11y-demo.cmx";
constexpr zx::duration kTimeout = zx::sec(15);

class FlutterSemanticsTests : public SemanticsIntegrationTest {
 public:
  FlutterSemanticsTests() : SemanticsIntegrationTest("flutter_semantics_test") {}

  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(SemanticsIntegrationTest::SetUp());

    view_manager()->SetSemanticsEnabled(true);

    scenic::EmbeddedViewInfo flutter_runner =
        scenic::LaunchComponentAndCreateView(environment()->launcher_ptr(), kClientUrl);
    flutter_runner.controller.events().OnTerminated = [](auto...) { FAIL(); };

    view_ref_koid_ = fsl::GetKoid(flutter_runner.view_ref.reference.get());

    // Present the view.
    embedder_view_.emplace(scenic::ViewContext{
        .session_and_listener_request = scenic::CreateScenicSessionPtrAndListenerRequest(scenic()),
        .view_token = CreatePresentationViewToken(),
    });

    // Embed the view.
    bool is_rendering = false;
    embedder_view_->EmbedView(std::move(flutter_runner),
                              [&is_rendering](fuchsia::ui::gfx::ViewState view_state) {
                                is_rendering = view_state.is_rendering;
                              });
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&is_rendering] { return is_rendering; }, kTimeout));

    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [&] {
          auto node = view_manager()->GetSemanticNode(view_ref_koid_, 0u);
          return node != nullptr && node->has_attributes() && node->attributes().has_label();
        },
        kTimeout))
        << "No root node found.";

    // a11y-demo's semantic tree:
    // ID: 0 Label:
    //   ID: 1 Label:Blue tapped 0 times
    //   ID: 2 Label:Yellow tapped 0 times
    //   ID: 3 Label:Blue
    //   ID: 4 Label:Yellow
  }

  zx_koid_t view_ref_koid() const { return view_ref_koid_; }

 private:
  // Wrapped in optional since the view is not created until the middle of SetUp
  std::optional<scenic::EmbedderView> embedder_view_;

  zx_koid_t view_ref_koid_;
};

// Loads ally-demo flutter app and verifies its semantic tree.
TEST_F(FlutterSemanticsTests, StaticSemantics) {
  auto root = view_manager()->GetSemanticNode(view_ref_koid(), 0u);
  auto node = FindNodeWithLabel(root, view_ref_koid(), "Blue tapped 0 times");
  ASSERT_TRUE(node);

  node = FindNodeWithLabel(root, view_ref_koid(), "Yellow tapped 0 times");
  ASSERT_TRUE(node);

  node = FindNodeWithLabel(root, view_ref_koid(), "Blue");
  ASSERT_TRUE(node);

  node = FindNodeWithLabel(root, view_ref_koid(), "Yellow");
  ASSERT_TRUE(node);
}

// Loads ally-demo flutter app and validates hit testing
TEST_F(FlutterSemanticsTests, HitTesting) {
  auto root = view_manager()->GetSemanticNode(view_ref_koid(), 0u);

  // We'll target all hits just within the bounding box of the nodes.
  fuchsia::math::PointF offset{1., 1.};

  // Hit test something with an action
  auto node = FindNodeWithLabel(root, view_ref_koid(), "Blue");
  ASSERT_TRUE(node);
  auto hit_node = HitTest(view_ref_koid(), CalculateViewTargetPoint(view_ref_koid(), node, offset));
  ASSERT_TRUE(hit_node.has_value());
  ASSERT_EQ(*hit_node, node->node_id());

  // Hit test a label
  node = FindNodeWithLabel(root, view_ref_koid(), "Yellow tapped 0 times");
  ASSERT_TRUE(node);
  hit_node = HitTest(view_ref_koid(), CalculateViewTargetPoint(view_ref_koid(), node, offset));
  ASSERT_TRUE(hit_node.has_value());
  ASSERT_EQ(*hit_node, node->node_id());
}

}  // namespace
}  // namespace accessibility_test
