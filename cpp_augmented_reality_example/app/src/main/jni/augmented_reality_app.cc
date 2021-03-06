/*
 * Copyright 2014 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <sstream>
#include <string>

#include <tango-gl/conversions.h>
#include <tango_support_api.h>

#include "tango-augmented-reality/augmented_reality_app.h"

// This blank namespace used for all the callbacks
namespace {
const int kVersionStringLength = 128;
// The minimum Tango Core version required from this application.
const int kTangoCoreMinimumVersion = 9377;

// Far clipping plane of the AR camera.
const float kArCameraNearClippingPlane = 0.1f;
const float kArCameraFarClippingPlane = 100.0f;

// This function routes onTangoEvent callbacks to the application object for
// handling.
//
// @param context, context will be a pointer to a AugmentedRealityApp
//        instance on which to call callbacks.
// @param   event, TangoEvent to route to onTangoEventAvailable function.
void onTangoEventAvailableRouter(void* context, const TangoEvent* event) {
  tango_augmented_reality::AugmentedRealityApp* app =
      static_cast<tango_augmented_reality::AugmentedRealityApp*>(context);
  app->onTangoEventAvailable(event);
}

// This function routes texture callbacks to the application object for
// handling.
//
// @param context, context will be a pointer to a AugmentedRealityApp
//        instance on which to call callbacks.
// @param id, id of the updated camera..
void onTextureAvailableRouter(void* context, TangoCameraId id) {
  tango_augmented_reality::AugmentedRealityApp* app =
      static_cast<tango_augmented_reality::AugmentedRealityApp*>(context);
  app->onTextureAvailable(id);
}

/**
* This function will route callbacks to our application object via the context
* parameter.
*
* @param context Will be a pointer to a PlaneFittingApplication instance on
* which to call callbacks.
* @param point_cloud The point cloud to pass on.
*/
void OnPointCloudAvailableRouter(void* context,
                                 const TangoPointCloud* point_cloud) {
    tango_augmented_reality::AugmentedRealityApp* app =
        static_cast<tango_augmented_reality::AugmentedRealityApp*>(context);
  app->OnPointCloudAvailable(point_cloud);
}

}  // namespace

namespace tango_augmented_reality {

void AugmentedRealityApp::OnSetScale(int scaleSize, bool callback = true) {
  //glm::vec3 debug = main_scene_.debugPosition();
  // __android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"X: %.4f    Y: %.4f   Z: %.4f\n", debug.x, debug.y, debug.z);
  //scaleSet = scaleSize;

  // is 0 - 10 -> .3 - 1.0
  float newBrightness = (static_cast<float>(scaleSize) * 7.0 + 30.0) / 100.0;
  main_scene_.SetBrightness( newBrightness );

  if (!callback) {
    char buf[32];
    int temp = scaleSize;
    sprintf(buf, "%d", temp);
    client_socket.broadcast(1, 0, std::string(buf));
  }
}

void AugmentedRealityApp::onTangoEventAvailable(const TangoEvent* event) {
  std::lock_guard<std::mutex> lock(tango_event_mutex_);
  tango_event_data_.UpdateTangoEvent(event);
}

void AugmentedRealityApp::onTextureAvailable(TangoCameraId id) {
  if (id == TANGO_CAMERA_COLOR) {
    RequestRender();
  }
}

void AugmentedRealityApp::OnPointCloudAvailable(
    const TangoPointCloud* point_cloud) {
  TangoSupport_updatePointCloud(point_cloud_manager_, point_cloud);
}

void AugmentedRealityApp::OnCreate(JNIEnv* env, jobject activity,
                                   int display_rotation) {
  // Check the installed version of the TangoCore.  If it is too old, then
  // it will not support the most up to date features.
  int version;
  TangoErrorType err = TangoSupport_GetTangoVersion(env, activity, &version);
  if (err != TANGO_SUCCESS || version < kTangoCoreMinimumVersion) {
    LOGE("AugmentedRealityApp::OnCreate, Tango Core version is out of date.");
    std::exit(EXIT_SUCCESS);
  }

  // We want to be able to trigger rendering on demand in our Java code.
  // As such, we need to store the activity we'd like to interact with and the
  // id of the method we'd like to call on that activity.
  calling_activity_obj_ = env->NewGlobalRef(activity);
  jclass cls = env->GetObjectClass(activity);
  on_demand_render_ = env->GetMethodID(cls, "requestRender", "()V");
  on_moon_update_ui_ = env->GetMethodID(cls, "updateMoonUI", "(I)V");

  is_service_connected_ = false;
  is_gl_initialized_ = false;

  display_rotation_ = display_rotation;
  is_video_overlay_rotation_set_ = false;
}

void AugmentedRealityApp::OnTangoServiceConnected(JNIEnv* env,
                                                  jobject iBinder) {
  TangoErrorType ret = TangoService_setBinder(env, iBinder);
  if (ret != TANGO_SUCCESS) {
    LOGE(
        "AugmentedRealityApp: Failed to set Tango binder with"
        "error code: %d",
        ret);
    std::exit(EXIT_SUCCESS);
  }

  TangoSetupConfig();
  TangoConnectCallbacks();
  TangoConnect();

  is_service_connected_ = true;
  UpdateViewportAndProjectionMatrix();
}

void AugmentedRealityApp::OnDestroy() {
  JNIEnv* env;
  java_vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
  env->DeleteGlobalRef(calling_activity_obj_);

  calling_activity_obj_ = nullptr;
  on_demand_render_ = nullptr;
  on_moon_update_ui_ = nullptr;
}

void AugmentedRealityApp::TangoSetupConfig() {
  // Here, we'll configure the service to run in the way we'd want. For this
  // application, we'll start from the default configuration
  // (TANGO_CONFIG_DEFAULT). This enables basic motion tracking capabilities.
  tango_config_ = TangoService_getConfig(TANGO_CONFIG_DEFAULT);
  if (tango_config_ == nullptr) {
    LOGE("AugmentedRealityApp: Failed to get default config form");
    std::exit(EXIT_SUCCESS);
  }

  //TODO defaulted to true?
  // Set auto-recovery for motion tracking as requested by the user.
//  int ret =
//      TangoConfig_setBool(tango_config_, "config_enable_auto_recovery", true);
//  if (ret != TANGO_SUCCESS) {
//    LOGE(
//        "AugmentedRealityApp: config_enable_auto_recovery() failed with error"
//        "code: %d",
//        ret);
//    std::exit(EXIT_SUCCESS);
//  }

  // Enable color camera from config.
  int ret = TangoConfig_setBool(tango_config_, "config_enable_color_camera", true);
  if (ret != TANGO_SUCCESS) {
    LOGE(
        "AugmentedRealityApp: config_enable_color_camera() failed with error"
        "code: %d",
        ret);
    std::exit(EXIT_SUCCESS);
  }

  // Low latency IMU integration enables aggressive integration of the latest
  // inertial measurements to provide lower latency pose estimates. This will
  // improve the AR experience.
  ret = TangoConfig_setBool(tango_config_,
                            "config_enable_low_latency_imu_integration", true);
  if (ret != TANGO_SUCCESS) {
    LOGE(
        "AugmentedRealityApp: config_enable_low_latency_imu_integration() "
        "failed with error code: %d",
        ret);
    std::exit(EXIT_SUCCESS);
  }

  // Drift correction allows motion tracking to recover after it loses tracking.
  //
  // The drift corrected pose is is available through the frame pair with
  // base frame AREA_DESCRIPTION and target frame DEVICE.
  ret = TangoConfig_setBool(tango_config_, "config_enable_drift_correction",
                            true);
  if (ret != TANGO_SUCCESS) {
    LOGE(
        "AugmentedRealityApp: enabling config_enable_drift_correction "
        "failed with error code: %d",
        ret);
    std::exit(EXIT_SUCCESS);
  }

  ret =
      TangoConfig_setBool(tango_config_, "config_enable_depth", true);
  if (ret != TANGO_SUCCESS) {
    LOGE("PlaneFittingApplication::TangoSetupConfig, Failed to enable depth.");
    std::exit(EXIT_SUCCESS);
  }

  ret = TangoConfig_setInt32(tango_config_, "config_depth_mode",
                             TANGO_POINTCLOUD_XYZC);
  if (ret != TANGO_SUCCESS) {
    LOGE("PlaneFittingApplication::TangoSetupConfig, Failed to configure to "
             "XYZC.");
    std::exit(EXIT_SUCCESS);
  }

  if (point_cloud_manager_ == nullptr) {
    int32_t max_point_cloud_elements;
    ret = TangoConfig_getInt32(tango_config_, "max_point_cloud_elements",
                               &max_point_cloud_elements);
    if (ret != TANGO_SUCCESS) {
      LOGE(
          "PlaneFittingApplication::TangoSetupConfig, Failed to query maximum "
              "number of point cloud elements.");
      std::exit(EXIT_SUCCESS);
    }

    ret = TangoSupport_createPointCloudManager(max_point_cloud_elements,
                                               &point_cloud_manager_);
    if (ret != TANGO_SUCCESS) {
      LOGE(
          "PlaneFittingApplication::TangoSetupConfig, Failed to create a "
              "point cloud manager.");
      std::exit(EXIT_SUCCESS);
    }
  }

  // Get TangoCore version string from service.
  char tango_core_version[kVersionStringLength];
  ret = TangoConfig_getString(tango_config_, "tango_service_library_version",
                              tango_core_version, kVersionStringLength);
  if (ret != TANGO_SUCCESS) {
    LOGE(
        "AugmentedRealityApp: get tango core version failed with error"
        "code: %d",
        ret);
    std::exit(EXIT_SUCCESS);
  }
  tango_core_version_string_ = tango_core_version;
}

void AugmentedRealityApp::TangoConnectCallbacks() {
  // Connect color camera texture.
  TangoErrorType ret = TangoService_connectOnTextureAvailable(
      TANGO_CAMERA_COLOR, this, onTextureAvailableRouter);
  if (ret != TANGO_SUCCESS) {
    LOGE(
        "AugmentedRealityApp: Failed to connect texture callback with error"
        "code: %d",
        ret);
    std::exit(EXIT_SUCCESS);
  }

  // Attach onEventAvailable callback.
  // The callback will be called after the service is connected.
  ret = TangoService_connectOnTangoEvent(onTangoEventAvailableRouter);
  if (ret != TANGO_SUCCESS) {
    LOGE(
        "AugmentedRealityApp: Failed to connect to event callback with error"
        "code: %d",
        ret);
    std::exit(EXIT_SUCCESS);
  }

  // Register for depth notification.
  ret = TangoService_connectOnPointCloudAvailable(OnPointCloudAvailableRouter);
  if (ret != TANGO_SUCCESS) {
    LOGE("Failed to connected to depth callback.");
    std::exit(EXIT_SUCCESS);
  }

}

// Connect to Tango Service, service will start running, and
// pose can be queried.
void AugmentedRealityApp::TangoConnect() {
  TangoErrorType ret = TangoService_connect(this, tango_config_);
  if (ret != TANGO_SUCCESS) {
    LOGE(
        "AugmentedRealityApp: Failed to connect to the Tango service with"
        "error code: %d",
        ret);
    std::exit(EXIT_SUCCESS);
  }

  // Initialize TangoSupport context.
  TangoSupport_initializeLibrary();

  // Sets up websocket

  websocket_connected = client_socket.connectSocket("24.240.32.197", 6419);
  __android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"connected: %d \n", websocket_connected);
  client_socket.setEvent(1, new_brightness);
  client_socket.setEvent(2, new_earth_toggle);
  client_socket.setEvent(3, new_moon_toggle);
}

void AugmentedRealityApp::OnPause() {
  TangoDisconnect();
  DeleteResources();
}

void AugmentedRealityApp::TangoDisconnect() {
  // When disconnecting from the Tango Service, it is important to make sure to
  // free your configuration object. Note that disconnecting from the service,
  // resets all configuration, and disconnects all callbacks. If an application
  // resumes after disconnecting, it must re-register configuration and
  // callbacks with the service.
  is_service_connected_ = false;
  is_gl_initialized_ = false;
  is_video_overlay_rotation_set_ = false;
  TangoConfig_free(tango_config_);
  tango_config_ = nullptr;
  TangoService_disconnect();
  TangoSupport_freePointCloudManager(point_cloud_manager_);
  point_cloud_manager_ = nullptr;
}

void AugmentedRealityApp::OnSurfaceCreated(AAssetManager* aasset_manager) {
  main_scene_.InitGLContent(aasset_manager);
  is_gl_initialized_ = true;
  UpdateViewportAndProjectionMatrix();
}

void AugmentedRealityApp::OnSurfaceChanged(int width, int height) {
  viewport_width_ = width;
  viewport_height_ = height;

  UpdateViewportAndProjectionMatrix();
}

void AugmentedRealityApp::UpdateViewportAndProjectionMatrix() {
  if (!is_service_connected_ || !is_gl_initialized_) {
    return;
  }
  // Query intrinsics for the color camera from the Tango Service, because we
  // want to match the virtual render camera's intrinsics to the physical
  // camera, we will compute the actually projection matrix and the view port
  // ratio for the render.
  float image_plane_ratio = 0.0f;

  int ret = TangoSupport_getCameraIntrinsicsBasedOnDisplayRotation(
      TANGO_CAMERA_COLOR,
      static_cast<TangoSupportRotation>(display_rotation_),
      &color_camera_intrinsics_);

  if (ret != TANGO_SUCCESS) {
    LOGE(
        "AugmentedRealityApp: Failed to get camera intrinsics with error"
        "code: %d",
        ret);
    return;
  }

  float image_width = static_cast<float>(color_camera_intrinsics_.width);
  float image_height = static_cast<float>(color_camera_intrinsics_.height);
  float fx = static_cast<float>(color_camera_intrinsics_.fx);
  float fy = static_cast<float>(color_camera_intrinsics_.fy);
  float cx = static_cast<float>(color_camera_intrinsics_.cx);
  float cy = static_cast<float>(color_camera_intrinsics_.cy);

  //glm::mat4 projection_mat_ar;
  projection_mat_ar = tango_gl::Camera::ProjectionMatrixForCameraIntrinsics(
      image_width, image_height, fx, fy, cx, cy, kArCameraNearClippingPlane,
      kArCameraFarClippingPlane);
  image_plane_ratio = image_height / image_width;

  //sets camera's projection matrix
  main_scene_.SetProjectionMatrix(projection_mat_ar);

  float screen_ratio = static_cast<float>(viewport_height_) /
                       static_cast<float>(viewport_width_);


  // In the following code, we place the view port at (0, 0) from the bottom
  // left corner of the screen. By placing it at (0,0), the view port may not
  // be exactly centered on the screen. However, this won't affect AR
  // visualization as the correct registration of AR objects relies on the
  // aspect ratio of the screen and video overlay, but not the position of the
  // view port.
  //
  // To place the view port in the center of the screen, please use following
  // code:
  //
  // if (image_plane_ratio < screen_ratio) {
  //   glViewport(-(h / image_plane_ratio - w) / 2, 0,
  //              h / image_plane_ratio, h);
  // } else {
  //   glViewport(0, -(w * image_plane_ratio - h) / 2, w,
  //              w * image_plane_ratio);
  // }

  if (image_plane_ratio < screen_ratio) {
    main_scene_.SetupViewport(viewport_height_ / image_plane_ratio,
                              viewport_height_);
  } else {
    main_scene_.SetupViewport(viewport_width_,
                              viewport_width_ * image_plane_ratio);
  }
}

void AugmentedRealityApp::OnDeviceRotationChanged(int display_rotation) {
  display_rotation_ = display_rotation;
  is_video_overlay_rotation_set_ = false;
}

void AugmentedRealityApp::OnDrawFrame() {
  // If tracking is lost, further down in this method Scene::Render
  // will not be called. Prevent flickering that would otherwise
  // happen by rendering solid white as a fallback.
  main_scene_.Clear();

  if (!is_gl_initialized_ || !is_service_connected_) {
    return;
  }

  if (!is_video_overlay_rotation_set_) {
    main_scene_.SetVideoOverlayRotation(display_rotation_, color_camera_intrinsics_);
    is_video_overlay_rotation_set_ = true;
  }

  TangoErrorType status = TangoService_updateTextureExternalOes(
      TANGO_CAMERA_COLOR, main_scene_.GetVideoOverlayTextureId(),
      &video_overlay_timestamp);

  if (status == TANGO_SUCCESS) {
    // When drift correction mode is enabled in config file, we need to query
    // the device with respect to Area Description pose in order to use the
    // drift corrected pose.
    //
    // Note that if you don't want to use the drift corrected pose, the
    // normal device with respect to start of service pose is still available.
    TangoDoubleMatrixTransformData matrix_transform;
    status = TangoSupport_getDoubleMatrixTransformAtTime(
        video_overlay_timestamp, TANGO_COORDINATE_FRAME_AREA_DESCRIPTION,
        TANGO_COORDINATE_FRAME_CAMERA_COLOR, TANGO_SUPPORT_ENGINE_OPENGL,
        TANGO_SUPPORT_ENGINE_OPENGL,
        static_cast<TangoSupportRotation>(display_rotation_),
        &matrix_transform);
    if (matrix_transform.status_code == TANGO_POSE_VALID) {
      {
        std::lock_guard<std::mutex> lock(transform_mutex_);
        UpdateTransform(matrix_transform.matrix, video_overlay_timestamp);
      }

      main_scene_.RotateEarthForTimestamp(video_overlay_timestamp);
      main_scene_.RotateMoonForTimestamp(video_overlay_timestamp);
      main_scene_.TranslateMoonForTimestamp(video_overlay_timestamp);

      main_scene_.Render(cur_start_service_T_camera_, projection_mat_ar);
    } else {
      // When the pose status is not valid, it indicates the tracking has
      // been lost. In this case, we simply stop rendering.
      //
      // This is also the place to display UI to suggest the user walk
      // to recover tracking.
      LOGE(
          "AugmentedRealityApp: Could not find a valid matrix transform at "
          "time %lf for the color camera.",
          video_overlay_timestamp);
    }
  } else {
    LOGE(
        "AugmentedRealityApp: Failed to update video overlay texture with "
        "error code: %d",
        status);
  }
}

void AugmentedRealityApp::DeleteResources() {
  main_scene_.DeleteResources();
  is_gl_initialized_ = false;
}

std::string AugmentedRealityApp::GetTransformString() {
  std::lock_guard<std::mutex> lock(transform_mutex_);
  return transform_string_;
}

std::string AugmentedRealityApp::GetEventString() {
  std::lock_guard<std::mutex> lock(tango_event_mutex_);
  return tango_event_data_.GetTangoEventString().c_str();
}

std::string AugmentedRealityApp::GetVersionString() {
  return tango_core_version_string_.c_str();
}

void AugmentedRealityApp::RequestRender() {
  if (calling_activity_obj_ == nullptr || on_demand_render_ == nullptr) {
    LOGE("Can not reference Activity to request render");
    return;
  }

  // Here, we notify the Java activity that we'd like it to trigger a render.
  JNIEnv* env;
  java_vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
  env->CallVoidMethod(calling_activity_obj_, on_demand_render_);
//

}

void AugmentedRealityApp::UpdateTransform(const double transform[16],
                                          double timestamp) {
  prev_start_service_T_camera_ = cur_start_service_T_camera_;
  cur_start_service_T_camera_ = glm::make_mat4(transform);
  // Increase pose counter.
  ++transform_counter_;
  prev_timestamp_ = cur_timestamp_;
  cur_timestamp_ = timestamp;
  FormatTransformString();
}

void AugmentedRealityApp::FormatTransformString() {
  const float* transform =
      (const float*)glm::value_ptr(cur_start_service_T_camera_);
  std::stringstream string_stream;
  string_stream.setf(std::ios_base::fixed, std::ios_base::floatfield);
  string_stream.precision(3);
  string_stream << "count: " << transform_counter_
                << ", delta time (ms): " << (cur_timestamp_ - prev_timestamp_)
                << std::endl << "position (m): [" << transform[12] << ", "
                << transform[13] << ", " << transform[14] << "]" << std::endl
                << "rotation matrix: [" << transform[0] << ", " << transform[1]
                << ", " << transform[2] << ", " << transform[4] << ", "
                << transform[5] << ", " << transform[6] << ", " << transform[8]
                << ", " << transform[9] << ", " << transform[10] << "]";
  transform_string_ = string_stream.str();
  string_stream.flush();
}

glm::mat4 AugmentedRealityApp::GetAreaDescriptionTDepthTransform(
      double timestamp) {
  glm::mat4 area_description_opengl_T_depth_tango;
  TangoMatrixTransformData matrix_transform;

  // When drift correction mode is enabled in config file, we need to query
  // the device with respect to Area Description pose in order to use the
  // drift corrected pose.
  //
  // Note that if you don't want to use the drift corrected pose, the
  // normal device with respect to start of service pose is still available.
  TangoSupport_getMatrixTransformAtTime(
      timestamp, TANGO_COORDINATE_FRAME_AREA_DESCRIPTION,
      TANGO_COORDINATE_FRAME_CAMERA_DEPTH, TANGO_SUPPORT_ENGINE_OPENGL,
      TANGO_SUPPORT_ENGINE_TANGO, ROTATION_IGNORED, &matrix_transform);
  if (matrix_transform.status_code != TANGO_POSE_VALID) {
    // When the pose status is not valid, it indicates the tracking has
    // been lost. In this case, we simply stop rendering.
    //
    // This is also the place to display UI to suggest the user walk
    // to recover tracking.
    LOGE(
        "PlaneFittingApplication: Could not find a valid matrix transform at "
            "time %lf for the color camera.",
        video_overlay_timestamp);
  } else {
    area_description_opengl_T_depth_tango =
        glm::make_mat4(matrix_transform.matrix);
  }
  return area_description_opengl_T_depth_tango;
}

void AugmentedRealityApp::magic() {
  TangoPointCloud* point_cloud = nullptr;
  TangoSupport_getLatestPointCloud(point_cloud_manager_, &point_cloud);
  if (point_cloud == nullptr) {
    return;
  }
  //__android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"point cloud: %d \n", (int)point_cloud->num_points );

  // Calculate the conversion from the latest color camera position to the
  // most recent depth camera position. This corrects for screen lag between
  // the two systems.
  TangoPoseData pose_depth_camera_t0_T_color_camera_t1;

  int ret = TangoSupport_calculateRelativePose(
      point_cloud->timestamp, TANGO_COORDINATE_FRAME_CAMERA_DEPTH,
      video_overlay_timestamp, TANGO_COORDINATE_FRAME_CAMERA_COLOR,
      &pose_depth_camera_t0_T_color_camera_t1);

  if (ret != TANGO_SUCCESS) {
    LOGE("%s: could not calculate relative pose", __func__);
    return;
  }

  // middle of screen
  glm::vec2 uv(.5, .5);

  double identity_translation[3] = {0.0, 0.0, 0.0};
  double identity_orientation[4] = {0.0, 0.0, 0.0, 1.0};
  glm::dvec3 double_depth_position;
  glm::dvec4 double_depth_plane_equation;

  if (TangoSupport_fitPlaneModelNearPoint(
      point_cloud, identity_translation, identity_orientation,
      glm::value_ptr(uv), static_cast<TangoSupportRotation>(display_rotation_),
      pose_depth_camera_t0_T_color_camera_t1.translation,
      pose_depth_camera_t0_T_color_camera_t1.orientation,
      glm::value_ptr(double_depth_position),
      glm::value_ptr(double_depth_plane_equation)) != TANGO_SUCCESS) {
    return;  // Assume error has already been reported.
  }

  const glm::vec3 depth_position =
      static_cast<glm::vec3>(double_depth_position);
  const glm::vec4 depth_plane_equation =
      static_cast<glm::vec4>(double_depth_plane_equation);


  const glm::mat4 area_description_opengl_T_depth_tango =
      GetAreaDescriptionTDepthTransform(point_cloud->timestamp);

  // Transform to Area Description coordinates
  const glm::vec4 area_description_position =
      area_description_opengl_T_depth_tango * glm::vec4(depth_position, 1.0f);

  glm::vec4 area_description_plane_equation;

  PlaneTransform(depth_plane_equation, area_description_opengl_T_depth_tango,
                 &area_description_plane_equation);

  const glm::vec3 plane_normal(area_description_plane_equation);


  // Use world up as the second vector, unless they are nearly parallel.
  // In that case use world +Z.
  glm::vec3 normal_Y = glm::vec3(0.0f, 1.0f, 0.0f);
  const glm::vec3 world_up = glm::vec3(0.0f, 1.0f, 0.0f);
  const float kWorldUpThreshold = 0.5f;
  if (glm::dot(plane_normal, world_up) > kWorldUpThreshold) {
    normal_Y = glm::vec3(0.0f, 0.0f, 1.0f);
  }

  const glm::vec3 normal_Z = glm::normalize(glm::cross(plane_normal, normal_Y));
  normal_Y = glm::normalize(glm::cross(normal_Z, plane_normal));

  glm::mat3 rotation_matrix;
  rotation_matrix[0] = plane_normal;
  rotation_matrix[1] = normal_Y;
  rotation_matrix[2] = normal_Z;
  const glm::quat rotation = glm::toQuat(rotation_matrix);

  main_scene_.SetNewPosition(glm::vec3(area_description_position) +
                              plane_normal * 0.05f);
}


void AugmentedRealityApp::EarthToggle(bool isChecked,  bool callback) {
  main_scene_.earth_check = isChecked;

  if (!callback) {
    if (isChecked) {
      client_socket.broadcast(2, 0, "true");
    } else {
      client_socket.broadcast(2, 0, "false");
    }
  } else {
//    if (isChecked) {
//      test = 1;
//    } else {
//      test = 2;
//    }
//    updateGUI = true;
  }
}

void AugmentedRealityApp::MoonToggle(bool isChecked,  bool callback) {
  main_scene_.moon_check = isChecked;

  if (!callback) {
    if (isChecked) {
      client_socket.broadcast(3, 0, "true");
    } else {
      client_socket.broadcast(3, 0, "false");
    }
  } else {
//    if (calling_activity_obj_ == nullptr || on_moon_update_ui_ == nullptr) {
//      LOGE("Can not reference Activity to request render");
//      return;
//    }
//
//    // Here, we notify the Java activity that we'd like it to trigger a render.
//    JNIEnv* env;
//    java_vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
//    env->CallVoidMethod(calling_activity_obj_, on_moon_update_ui_);
  }
}


}  // namespace tango_augmented_reality

void new_brightness(char *body) {

  char *end_ptr;
  int bright_value = strtol(body, &end_ptr, 10);

  if (errno == ERANGE || body == end_ptr) {
    return;

  } else if (bright_value < 0 || bright_value > 10) {
    return;
  } else {
    app.OnSetScale(bright_value, true);
  }

}

void new_earth_toggle(char *body) {
  if (strncasecmp(body, "true", 4) == 0) {
    app.EarthToggle(true, true);
  } else if (strncasecmp(body, "false", 5) == 0) {
    app.EarthToggle(false, true);
  }
}

void new_moon_toggle(char *body) {
  if (strncasecmp(body, "true", 4) == 0) {
    app.MoonToggle(true, true);
  } else if (strncasecmp(body, "false", 5) == 0) {
    app.MoonToggle(false, true);
  }
}
