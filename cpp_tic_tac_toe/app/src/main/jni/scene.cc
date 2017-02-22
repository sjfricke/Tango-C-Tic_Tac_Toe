//
// Created by SpencerFricke on 2/19/2017.
//

#include <math.h>

#include <tango-gl/conversions.h>
#include <tango-gl/tango-gl.h>
#include <tango-gl/texture.h>
#include <tango-gl/shaders.h>
#include <tango-gl/meshes.h>

#include "tango-plane-fitting/scene.h"

namespace tango_plane_fitting {

    Scene::Scene() {}

    Scene::~Scene() {}

    void Scene::InitGLContent(AAssetManager* aasset_manager) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        // Allocating render camera and drawable object.
        // All of these objects are for visualization purposes.
        video_overlay_ = new tango_gl::VideoOverlay();
        camera_ = new tango_gl::Camera();

        // Init mido mesh and material
        mido_mesh_ = new tango_gl::StaticMesh();
        mido_material_ = new tango_gl::Material();
        mido_texture_ = new tango_gl::Texture(aasset_manager, "Mido_grp.png");

        mido_material_->SetShader(
                tango_gl::shaders::GetTexturedVertexShader().c_str(),
                tango_gl::shaders::GetTexturedFragmentShader().c_str());
        mido_material_->SetParam("texture", mido_texture_);

        mido_transform_.SetPosition(glm::vec3(0.0f, 0.0f, -5.0f));

        is_content_initialized_ = true;
    }

    void Scene::DeleteResources() {
        if (is_content_initialized_) {
            delete camera_;
            camera_ = nullptr;
            delete video_overlay_;
            video_overlay_ = nullptr;
            delete mido_mesh_;
            mido_mesh_ = nullptr;
            delete mido_material_;
            mido_material_ = nullptr;
            delete mido_texture_;
            mido_texture_ = nullptr;

            is_content_initialized_ = false;
        }
    }

    void Scene::SetupViewport(int w, int h) {
        if (h <= 0 || w <= 0) {
            LOGE("Setup graphic height not valid");
            return;
        }

        viewport_width_ = w;
        viewport_height_ = h;
    }

    void Scene::SetProjectionMatrix(const glm::mat4& projection_matrix) {
        camera_->SetProjectionMatrix(projection_matrix);
    }

    void Scene::Clear() {
        glViewport(0, 0, viewport_width_, viewport_height_);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    }

    void Scene::Render(const glm::mat4& cur_pose_transformation) {
        glViewport(0, 0, viewport_width_, viewport_height_);

        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

        // In first person mode, we directly control camera's motion.
        camera_->SetTransformationMatrix(cur_pose_transformation);

        // If it's first person view, we will render the video overlay in full
        // screen, so we passed identity matrix as view and projection matrix.
        glDisable(GL_DEPTH_TEST);
        video_overlay_->Render(glm::mat4(1.0f), glm::mat4(1.0f));
        glEnable(GL_DEPTH_TEST);

        tango_gl::Render(*mido_mesh_, *mido_material_, mido_transform_, *camera_);
    }

    void Scene::RotateMidoForTimestamp(double timestamp) {
        RotateYAxisForTimestamp(timestamp, &mido_transform_, &mido_last_angle_,
                                &mido_last_timestamp_);
    }
   /* void Scene::TranslateMoonForTimestamp(double timestamp) {
        if (moon_last_translation_timestamp_ > 0) {
            // Calculate time difference in seconds
            double delta_time = timestamp - moon_last_translation_timestamp_;
            // Calculate the corresponding angle movement considering
            // a total rotation time of 6 seconds
            double delta_angle = delta_time * 2 * M_PI / 6;
            // Add this angle to the last known angle
            double angle = moon_last_translation_angle_ + delta_angle;
            moon_last_translation_angle_ = angle;

            double x = 2.0f * sin(angle);
            double z = 2.0f * cos(angle);

            moon_transform_.SetPosition(glm::vec3(x, 0.0f, z - 5.0f));
        }
        moon_last_translation_timestamp_ = timestamp;
    }*/

    void Scene::RotateYAxisForTimestamp(double timestamp,
                                        tango_gl::Transform* transform,
                                        double* last_angle,
                                        double* last_timestamp) {
        if (*last_timestamp > 0) {
            // Calculate time difference in seconds
            double delta_time = timestamp - *last_timestamp;
            // Calculate the corresponding angle movement considering
            // a total rotation time of 6 seconds
            double delta_angle = delta_time * 2 * M_PI / 6;
            // Add this angle to the last known angle
            double angle = *last_angle + delta_angle;
            *last_angle = angle;

            double w = cos(angle / 2);
            double y = sin(angle / 2);

            transform->SetRotation(glm::quat(w, 0.0f, y, 0.0f));
        }
        *last_timestamp = timestamp;
    }

    void Scene::SetVideoOverlayRotation(int display_rotation) {
        if (is_content_initialized_) {
            video_overlay_->SetDisplayRotation(
                    static_cast<TangoSupportRotation>(display_rotation));
        }
    }

}  // namespace tango_plane_fitting