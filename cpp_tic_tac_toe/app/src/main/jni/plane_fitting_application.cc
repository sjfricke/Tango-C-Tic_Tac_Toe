/*
 * Copyright 2015 Google Inc. All Rights Reserved.
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

#include "tango-plane-fitting/plane_fitting_application.h"

#include <glm/gtc/matrix_access.hpp>
#include <glm/gtx/quaternion.hpp>
#include <tango-gl/camera.h>
#include <tango-gl/conversions.h>
#include <tango-gl/util.h>

#include <stdio.h>
#include <string>
#include <memory>
#include <functional>

#include "tango-plane-fitting/plane_fitting.h"
#include <tango_support_api.h>
#include <android/asset_manager.h>

#include <android/log.h>
#include <tango-gl/shaders.h>

namespace tango_plane_fitting {

    namespace {

        // The minimum Tango Core version required from this application.
        constexpr int kTangoCoreMinimumVersion = 9377;
        constexpr float kCubeScale = 0.05f;

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
  PlaneFittingApplication* app = static_cast<PlaneFittingApplication*>(context);
  app->OnPointCloudAvailable(point_cloud);
}

// This function does nothing. TangoService_connectOnTextureAvailable
// requires a callback function pointer, and it cannot be null.
void onTextureAvailableRouter(void*, TangoCameraId) { return; }

/**
 * Create an OpenGL perspective matrix from window size, camera intrinsics,
 * and clip settings.
 */
    glm::mat4 ProjectionMatrixForCameraIntrinsics(
            const TangoCameraIntrinsics& intrinsics, float near, float far) {
      // Adjust camera intrinsics according to rotation
      double cx = intrinsics.cx;
      double cy = intrinsics.cy;
      double width = intrinsics.width;
      double height = intrinsics.height;
      double fx = intrinsics.fx;
      double fy = intrinsics.fy;

      return tango_gl::Camera::ProjectionMatrixForCameraIntrinsics(
              width, height, fx, fy, cx, cy, near, far);
    }

}  // end namespace

void PlaneFittingApplication::OnPointCloudAvailable(
        const TangoPointCloud* point_cloud) {
  TangoSupport_updatePointCloud(point_cloud_manager_, point_cloud);
}

PlaneFittingApplication::PlaneFittingApplication()
        : point_cloud_debug_render_(false),
          last_gpu_timestamp_(0.0),
          is_service_connected_(false),
          is_gl_initialized_(false),
          is_scene_camera_configured_(false) {}

PlaneFittingApplication::~PlaneFittingApplication() {
  TangoConfig_free(tango_config_);
  TangoSupport_freePointCloudManager(point_cloud_manager_);
  point_cloud_manager_ = nullptr;
}

void PlaneFittingApplication::OnCreate(JNIEnv* env, jobject activity) {
  // Check that we have the minimum required version of Tango.
  int version;
  TangoErrorType err = TangoSupport_GetTangoVersion(env, activity, &version);
  if (err != TANGO_SUCCESS || version < kTangoCoreMinimumVersion) {
    LOGE(
            "PlaneFittingApplication::OnCreate, Tango Core version is out of "
                    "date.");
    std::exit(EXIT_SUCCESS);
  }
}

void PlaneFittingApplication::OnTangoServiceConnected(JNIEnv* env,
                                                      jobject binder) {
  TangoErrorType ret = TangoService_setBinder(env, binder);
  if (ret != TANGO_SUCCESS) {
    LOGE("PlaneFittingApplication: TangoService_setBinder error");
    std::exit(EXIT_SUCCESS);
  }

  TangoSetupConfig();
  TangoConnectCallbacks();
  TangoConnect();
  is_service_connected_ = true;
}

void PlaneFittingApplication::TangoSetupConfig() {
  // Here, we will configure the service to run in the way we would want. For
  // this application, we will start from the default configuration
  // (TANGO_CONFIG_DEFAULT). This enables basic motion tracking capabilities.
  // In addition to motion tracking, however, we want to run with depth so that
  // we can measure things. As such, we are going to set an additional flag
  // "config_enable_depth" to true.
  tango_config_ = TangoService_getConfig(TANGO_CONFIG_DEFAULT);
  if (tango_config_ == nullptr) {
    LOGE(
            "PlaneFittingApplication::TangoSetupConfig, Unable to get tango "
                    "config");
    std::exit(EXIT_SUCCESS);
  }

  TangoErrorType ret =
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

  ret = TangoConfig_setBool(tango_config_, "config_enable_color_camera", true);
  if (ret != TANGO_SUCCESS) {
    LOGE("PlaneFittingApplication::TangoSetupConfig, Failed to enable color "
                 "camera.");
    std::exit(EXIT_SUCCESS);
  }

  // Note that it is super important for AR applications that we enable low
  // latency IMU integration so that we have pose information available as
  // quickly as possible. Without setting this flag, you will often receive
  // invalid poses when calling getPoseAtTime() for an image.
  ret = TangoConfig_setBool(tango_config_,
                            "config_enable_low_latency_imu_integration", true);
  if (ret != TANGO_SUCCESS) {
    LOGE("PlaneFittingApplication::TangoSetupConfig, Failed to enable low "
                 "latency imu integration.");
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
            "PlaneFittingApplication::TangoSetupConfig, enabling "
                    "config_enable_drift_correction failed with error code: %d",
            ret);
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
}

void PlaneFittingApplication::TangoConnectCallbacks() {
  // Register for depth notification.
  TangoErrorType ret =
          TangoService_connectOnPointCloudAvailable(OnPointCloudAvailableRouter);
  if (ret != TANGO_SUCCESS) {
    LOGE("Failed to connected to depth callback.");
    std::exit(EXIT_SUCCESS);
  }

  // The Tango service allows you to connect an OpenGL texture directly to its
  // RGB and fisheye cameras. This is the most efficient way of receiving
  // images from the service because it avoids copies. You get access to the
  // graphic buffer directly. As we are interested in rendering the color image
  // in our render loop, we will be polling for the color image as needed.
  ret = TangoService_connectOnTextureAvailable(TANGO_CAMERA_COLOR, this,
                                               onTextureAvailableRouter);
  if (ret != TANGO_SUCCESS) {
    LOGE(
            "PlaneFittingApplication: Failed to connect texture callback with"
                    "error code: %d",
            ret);
    std::exit(EXIT_SUCCESS);
  }
}

void PlaneFittingApplication::TangoConnect() {
  // Here, we will connect to the TangoService and set up to run. Note that
  // we are passing in a pointer to ourselves as the context which will be
  // passed back in our callbacks.
  TangoErrorType ret = TangoService_connect(this, tango_config_);
  if (ret != TANGO_SUCCESS) {
    LOGE("PlaneFittingApplication: Failed to connect to the Tango service.");
    std::exit(EXIT_SUCCESS);
  }

  // Get the intrinsics for the color camera and pass them on to the depth
  // image. We need these to know how to project the point cloud into the color
  // camera frame.
  ret = TangoService_getCameraIntrinsics(TANGO_CAMERA_COLOR,
                                         &color_camera_intrinsics_);
  if (ret != TANGO_SUCCESS) {
    LOGE(
            "PlaneFittingApplication: Failed to get the intrinsics for the color"
                    "camera.");
    std::exit(EXIT_SUCCESS);
  }

  // Initialize TangoSupport context.
  TangoSupport_initializeLibrary();

  // Sets up websocket
  client_socket.connectSocket("24.240.32.197", 5000);
  client_socket.setEvent(2, new_cube_callback);
  //client_socket.setEvent(1, new_color_callback);
  //client_socket.setEvent(1, [this](char*x){this->on_new_color(x);} ) ;
}

void PlaneFittingApplication::OnPause() {
  is_service_connected_ = false;
  is_gl_initialized_ = false;
  TangoDisconnect();
  DeleteResources();
}

void PlaneFittingApplication::TangoDisconnect() { TangoService_disconnect(); }

void PlaneFittingApplication::OnSurfaceCreated(AAssetManager* aasset_manager) {

  video_overlay_ = new tango_gl::VideoOverlay();
  video_overlay_->SetDisplayRotation(display_rotation_);
  point_cloud_renderer_ = new PointCloudRenderer();

  for( int i = 0; i < max_cube; i++) {
    cube_[i] = new tango_gl::Cube();
    cube_[i]->SetScale(glm::vec3(kCubeScale, kCubeScale, kCubeScale));
  }
  cube_count = 0;

//  cube_ = new tango_gl::Cube();
//  cube_->SetScale(glm::vec3(kCubeScale, kCubeScale, kCubeScale));
//  cube_->SetColor(0.7f, 0.7f, 0.7f);
//  cube_->SetPosition(glm::vec3(0.0f, 0.0f, -2.0f));

  is_gl_initialized_ = true;
}

void PlaneFittingApplication::SetRenderDebugPointCloud(bool on) {
  point_cloud_renderer_->SetRenderDebugColors(on);
}

void PlaneFittingApplication::SetColorValue(int color_value) {
  __android_log_print(ANDROID_LOG_DEBUG, "ABC", "\n \"SetColorValue : %d \n", color_value);

  cube_color = color_value;
  /*if (color_value == 0) {
    cube_->SetColor(1.0f, 0.0f, 0.0f);
  } else if (color_value == 1) {
    cube_->SetColor(0.0f, 1.0f, 0.0f);
  } else if (color_value == 2) {
    cube_->SetColor(0.0f, 0.0f, 1.0f);
  }*/

//  if (color_value == 0) {
//    message = "0";
//  } else if (color_value == 1) {
//    message = "1";
//  } else if (color_value == 2) {
//    message = "2";
//  }

//  status = send(mySocket, message , MSG_SIZE, 0);
//  if (status < 0) { __android_log_print(ANDROID_LOG_DEBUG, "LOG_TAG", "Send failed"); }
//
//  printf("Sent Message\n");
//
//  // receive a reply from the server
//  status = recv(mySocket, server_reply, MSG_SIZE, 0);
//  if (status < 0) {
//    __android_log_print(ANDROID_LOG_DEBUG, "LOG_TAG", "ERROR %d: Reply failed %s",status, (char*)server_reply);
//  } else {
//    __android_log_print(ANDROID_LOG_DEBUG, "LOG_TAG", "Reply received: %s", (char*)server_reply);
//  }
//
//  if ( static_cast<char*>(server_reply)[0] == RED[0] ) {
//    cube_->SetColor(1.0f, 0.0f, 0.0f);
//  } else  if ( static_cast<char*>(server_reply)[0] == GREEN[0] ) {
//    cube_->SetColor(0.0f, 1.0f, 0.0f);
//  } else if ( static_cast<char*>(server_reply)[0] == BLUE[0] ) {
//    cube_->SetColor(0.0f, 0.0f, 1.0f);
//  }


}

void PlaneFittingApplication::on_new_color(char* body) {
  if (strncasecmp(body, "red", 3) == 0) {
    SetColorValue(0);
    __android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"on_new_color (%d) : %s \n", 0, body);
  } else if (strncasecmp(body, "green", 5)  == 0) {
    SetColorValue(1);
    __android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"on_new_color (%d) : %s \n", 1, body);

  } else if (strncasecmp(body, "blue", 4)  == 0) {
    SetColorValue(2);
    __android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"on_new_color (%d) : %s \n", 2, body);

  } else {
    __android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"on_new_color (%d) : %s \n", -1, body);

  };

}

void PlaneFittingApplication::on_new_cube(char* body) {

  __android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"%s\n", body);

  char *tokPtr;
  tokPtr = strtok(body, ",");
  float x_p = atof(tokPtr);
  tokPtr = strtok (NULL, ",");
  float y_p = atof(tokPtr);
  tokPtr = strtok (NULL, ",");
  float z_p = atof(tokPtr);

  tokPtr = strtok(NULL, ",");
  float x_r = atof(tokPtr);
  tokPtr = strtok (NULL, ",");
  float y_r = atof(tokPtr);
  tokPtr = strtok (NULL, ",");
  float z_r = atof(tokPtr);
  tokPtr = strtok (NULL, ",");
  float w_r = atof(tokPtr);

  tokPtr = strtok (NULL, ",");
  int cube_color_temp = atoi(tokPtr);

  if (cube_color_temp == 0) {
    cube_[cube_count]->SetColor(1.0f, 0.0f, 0.0f);
  } else if (cube_color_temp == 1) {
    cube_[cube_count]->SetColor(0.0f, 1.0f, 0.0f);
  } else if (cube_color_temp == 2) {
    cube_[cube_count]->SetColor(0.0f, 0.0f, 1.0f);
  }

  cube_[cube_count]->SetRotation(glm::quat(w_r, x_r, y_r, z_r));
  cube_[cube_count]->SetPosition(glm::vec3((reference_point.x + x_p), (reference_point.y + y_p), (reference_point.z + z_p)));

  cube_count++;

  //__android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"x: %.3f y: %.3f z: %.3f \n", x,y,z);
  //cube2_->SetPosition(glm::vec3(x, y, z));

}

void PlaneFittingApplication::BroadCastColorValue(int color_value) {
  if (color_value == 0) {
    client_socket.broadcast(1, 0, "red");
  } else if (color_value == 1) {
    client_socket.broadcast(1, 0, "green");
  } else if (color_value == 2) {
    client_socket.broadcast(1, 0, "blue");
  }

}

void PlaneFittingApplication::OnSurfaceChanged(int width, int height) {
  screen_width_ = static_cast<float>(width);
  screen_height_ = static_cast<float>(height);

  is_scene_camera_configured_ = false;
}

void PlaneFittingApplication::OnDrawFrame() {
  // If tracking is lost, further down in this method Scene::Render
  // will not be called. Prevent flickering that would otherwise
  // happen by rendering solid black as a fallback.
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

  if (!is_gl_initialized_ || !is_service_connected_) {
    return;
  }

  if (!is_scene_camera_configured_) {
    SetViewportAndProjectionGLThread();
    is_scene_camera_configured_ = true;
  }

  // We need to make sure that we update the texture associated with the color
  // image.
  if (TangoService_updateTextureExternalOes(
          TANGO_CAMERA_COLOR, video_overlay_->GetTextureId(),
          &last_gpu_timestamp_) != TANGO_SUCCESS) {
    LOGE("PlaneFittingApplication: Failed to get a color image.");
    return;
  }

  // Querying the GPU color image's frame transformation based its timestamp.
  TangoMatrixTransformData matrix_transform;
  TangoSupport_getMatrixTransformAtTime(
          last_gpu_timestamp_, TANGO_COORDINATE_FRAME_AREA_DESCRIPTION,
          TANGO_COORDINATE_FRAME_CAMERA_COLOR, TANGO_SUPPORT_ENGINE_OPENGL,
          TANGO_SUPPORT_ENGINE_OPENGL, display_rotation_, &matrix_transform);
  if (matrix_transform.status_code != TANGO_POSE_VALID) {
    LOGE(
            "PlaneFittingApplication: Could not find a valid matrix transform at "
                    "time %lf for the color camera.",
            last_gpu_timestamp_);
  } else {
    const glm::mat4 area_description_T_color_camera =
            glm::make_mat4(matrix_transform.matrix);
    GLRender(area_description_T_color_camera);
  }
}

void PlaneFittingApplication::GLRender(
        const glm::mat4& area_description_T_color_camera) {
  glEnable(GL_CULL_FACE);

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // We want to render from the perspective of the device, so we will set our
  // camera based on the transform that was passed in.
  glm::mat4 color_camera_T_area_description =
          glm::inverse(area_description_T_color_camera);

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  video_overlay_->Render(glm::mat4(1.0), glm::mat4(1.0));
  glEnable(GL_DEPTH_TEST);

  // gets data for point cloud render
  TangoPointCloud* point_cloud = nullptr;
  TangoSupport_getLatestPointCloud(point_cloud_manager_, &point_cloud);
  if (point_cloud != nullptr) {
    const glm::mat4 area_description_opengl_T_depth_t1_tango = GetAreaDescriptionTDepthTransform(point_cloud->timestamp);
    const glm::mat4 projection_T_depth = projection_matrix_ar_ * color_camera_T_area_description * area_description_opengl_T_depth_t1_tango;
    point_cloud_renderer_->Render(projection_T_depth, area_description_opengl_T_depth_t1_tango, point_cloud);
  }

  glDisable(GL_BLEND);


  for( int i = 0; i < cube_count; i++) {
    cube_[i]->Render(projection_matrix_ar_, color_camera_T_area_description);
  }
}

void PlaneFittingApplication::DeleteResources() {
  delete video_overlay_;
  //delete cube_;
  delete point_cloud_renderer_;
  video_overlay_ = nullptr;
  point_cloud_renderer_ = nullptr;
}

void PlaneFittingApplication::OnDisplayChanged(int display_rotation) {
  display_rotation_ =
          static_cast<TangoSupportRotation>(display_rotation);
  is_scene_camera_configured_ = false;
}

void PlaneFittingApplication::SetViewportAndProjectionGLThread() {
  if (!is_gl_initialized_ || !is_service_connected_) {
    return;
  }

  TangoSupport_getCameraIntrinsicsBasedOnDisplayRotation(
          TANGO_CAMERA_COLOR, display_rotation_, &color_camera_intrinsics_);

  video_overlay_->SetDisplayRotation(display_rotation_);
  video_overlay_->SetTextureOffset(
          screen_width_, screen_height_,
          static_cast<float>(color_camera_intrinsics_.width),
          static_cast<float>(color_camera_intrinsics_.height));

  glViewport(0, 0, screen_width_, screen_height_);

  constexpr float kNearPlane = 0.1f;
  constexpr float kFarPlane = 100.0f;
  projection_matrix_ar_ = ProjectionMatrixForCameraIntrinsics(
          color_camera_intrinsics_, kNearPlane, kFarPlane);
}

// We assume the Java layer ensures this function is called on the GL thread.
void PlaneFittingApplication::OnTouchEvent(float x, float y) {
  if (!is_gl_initialized_ || !is_service_connected_) {
    return;
  }

  // Get the latest point cloud
  TangoPointCloud* point_cloud = nullptr;
  TangoSupport_getLatestPointCloud(point_cloud_manager_, &point_cloud);
  if (point_cloud == nullptr) {
    return;
  }

  // Calculate the conversion from the latest color camera position to the
  // most recent depth camera position. This corrects for screen lag between
  // the two systems.
  TangoPoseData pose_depth_camera_t0_T_color_camera_t1;
  int ret = TangoSupport_calculateRelativePose(
          point_cloud->timestamp, TANGO_COORDINATE_FRAME_CAMERA_DEPTH,
          last_gpu_timestamp_, TANGO_COORDINATE_FRAME_CAMERA_COLOR,
          &pose_depth_camera_t0_T_color_camera_t1);
  if (ret != TANGO_SUCCESS) {
    LOGE("%s: could not calculate relative pose", __func__);
    return;
  }
  glm::vec2 uv(x / screen_width_, y / screen_height_);

  double identity_translation[3] = {0.0, 0.0, 0.0};
  double identity_orientation[4] = {0.0, 0.0, 0.0, 1.0};
  glm::dvec3 double_depth_position;
  glm::dvec4 double_depth_plane_equation;
  if (TangoSupport_fitPlaneModelNearPoint(
          point_cloud, identity_translation, identity_orientation,
          glm::value_ptr(uv), display_rotation_,
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

  point_cloud_renderer_->SetPlaneEquation(area_description_plane_equation);

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

  //  glm::vec3 test = glm::vec3(area_description_position) + plane_normal * kCubeScale;

//  __android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"Area_Des_pos: x: %.3f\ty: %.3f\tz: %.3f\nplane_normal: x: %.3f\ty: %.3f\tz: %.3f\npos: x: %.3f\ty: %.3f\tz: %.3f  \n",
//                      area_description_position.x, area_description_position.y, area_description_position.z,
//  plane_normal.x, plane_normal.y, plane_normal.z,
//  test.x, test.y, test.z);

//    __android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"depth_pos: x: %.3f\ty: %.3f\tz: %.3f\ndepth_plane: x: %.3f\ty: %.3f\tz: %.3f\npos: x: %.3f\ty: %.3f\tz: %.3f  \n",
//                       depth_position.x, depth_position.y, depth_position.z,
//                        depth_plane_equation.x, depth_plane_equation.y, depth_plane_equation.z,
//                       test.x, test.y, test.z);

    // first on touch sets reference point
    if (!reference_set) {
      reference_set = true;
      SetRenderDebugPointCloud(false);
      reference_point = glm::vec3(area_description_position);
      __android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"Set reference point \n");
      return;
    }


    if (cube_color == 0) {
      cube_[cube_count]->SetColor(1.0f, 0.0f, 0.0f);
    } else if (cube_color == 1) {
      cube_[cube_count]->SetColor(0.0f, 1.0f, 0.0f);
    } else if (cube_color == 2) {
      cube_[cube_count]->SetColor(0.0f, 0.0f, 1.0f);
    }

    glm::vec3 new_pos = glm::vec3(area_description_position) + plane_normal * kCubeScale;
    cube_[cube_count]->SetRotation(rotation);
    cube_[cube_count]->SetPosition(new_pos);

    __android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"x: %.3f y: %.3f z: %.3f w: %.3f\n", rotation.x, rotation.y, rotation.z, rotation.w);

    glm::quat rotationn = glm::quat(rotation.w, rotation.x, rotation.y, rotation.z);
     __android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"NEW x: %.3f y: %.3f z: %.3f w: %.3f\n", rotationn.x, rotationn.y, rotationn.z, rotationn.w);


    cube_count++;

    std::stringstream ss;
    ss << (new_pos.x - reference_point.x) <<  "," << (new_pos.y - reference_point.y) << "," << (new_pos.z - reference_point.z) << ","
       << rotation.x <<  "," << rotation.y << "," << rotation.z << "," << rotation.w << "," << cube_color;
    std::string s = ss.str();

    client_socket.broadcast(2, 0, s);
}


glm::mat4 PlaneFittingApplication::GetAreaDescriptionTDepthTransform(
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
            last_gpu_timestamp_);
  } else {
    area_description_opengl_T_depth_tango =
            glm::make_mat4(matrix_transform.matrix);
  }
  return area_description_opengl_T_depth_tango;
}

}  // namespace tango_plane_fitting

void new_color_callback(char *body) {
  __android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"new_color_callback : %s \n", body);
  app.on_new_color(body);
}

void new_cube_callback(char *body) {
  //__android_log_print(ANDROID_LOG_INFO, "ABC", "\n \"new_color_callback : %s \n", body);
  app.on_new_cube(body);
}
