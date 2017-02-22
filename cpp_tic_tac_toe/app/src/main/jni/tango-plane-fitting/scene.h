//
// Created by SpencerFricke on 2/19/2017.
//

#ifndef CPP_TIC_TAC_TOE_SCENE_H
#define CPP_TIC_TAC_TOE_SCENE_H

#include <jni.h>
#include <memory>
#include <math.h>

#include <android/asset_manager.h>

#include <tango_client_api.h>  // NOLINT
#include <tango-gl/camera.h>
#include <tango-gl/color.h>
#include <tango-gl/transform.h>
#include <tango-gl/util.h>
#include <tango-gl/video_overlay.h>
#include <tango-gl/texture.h>
#include <tango-gl/mesh.h>
#include <tango-gl/meshes.h>
#include <tango-gl/shaders.h>
#include <tango-gl/tango-gl.h>


namespace tango_plane_fitting {

// Scene provides OpenGL drawable objects and renders them for visualization.
    class Scene {
    public:
        // Constructor and destructor.
        Scene();
        ~Scene();

        // Allocate OpenGL resources for rendering.
        void InitGLContent(AAssetManager* aasset_manager);

        // Release non-OpenGL resources.
        void DeleteResources();

        // Setup GL view port.
        // @param: w, width of the screen.
        // @param: h, height of the screen.
        void SetupViewport(int w, int h);

        // Clear the screen to a solid color.
        void Clear();

        // Render loop.
        void Render(const glm::mat4& cur_pose_transformation);

        // Get video overlay texture id.
        // @return: texture id of video overlay's texture.
        GLuint GetVideoOverlayTextureId() { return video_overlay_->GetTextureId(); }

        // @return: AR render camera's image plane ratio.
        float GetCameraImagePlaneRatio() { return camera_image_plane_ratio_; }

        // Set AR render camera's image plane ratio.
        // @param: image plane ratio.
        void SetCameraImagePlaneRatio(float ratio) {
            camera_image_plane_ratio_ = ratio;
        }

        // @return: AR render camera's image plane distance from the view point.
        float GetImagePlaneDistance() { return image_plane_distance_; }

        // Set AR render camera's image plane distance from the view point.
        // @param: distance, AR render camera's image plane distance from the view
        //         point.
        void SetImagePlaneDistance(float distance) {
            image_plane_distance_ = distance;
        }

        // Set projection matrix of the AR view (first person view)
        // @param: projection_matrix, the projection matrix.
        void SetProjectionMatrix(const glm::mat4& projection_matrix);

        // Change the mido transformation to make it rotate over its Y axis over
        // time
        void RotateMidoForTimestamp(double timestamp);

        // Change the moon transformation to make it rotate over its Y axis over
        // time
        void RotateMoonForTimestamp(double timestamp);

        // Change the moon position to make it rotate around the mido over time
        void TranslateMoonForTimestamp(double timestamp);

        // Apply a Y axis rotate transform to object
        void RotateYAxisForTimestamp(double timestamp, tango_gl::Transform* transform,
                                     double* last_angle, double* last_timestamp);

        // Set video overlay's orientation based on current device orientation.
        void SetVideoOverlayRotation(int display_rotation);

    private:
        // Video overlay drawable object to display the camera image.
        tango_gl::VideoOverlay* video_overlay_;

        // Camera object that allows user to use touch input to interact with.
        tango_gl::Camera* camera_;

        // We use both camera_image_plane_ratio_ and image_plane_distance_ to compute
        // the first person AR camera's frustum, these value is derived from actual
        // physical camera instrinsics.
        // Aspect ratio of the color camera.
        float camera_image_plane_ratio_;

        // Image plane distance from camera's origin view point.
        float image_plane_distance_;

        // The projection matrix for the first person AR camera.
        glm::mat4 ar_camera_projection_matrix_;

        // Meshes
        tango_gl::StaticMesh* mido_mesh_;

        // Textures
        tango_gl::Texture* mido_texture_;

        // Materials
        tango_gl::Material* mido_material_;

        // Transforms
        tango_gl::Transform mido_transform_;

        // Last pose timestamp received
        double mido_last_timestamp_;
        double mido_last_angle_;

        // Check if resources is allocated.
        bool is_content_initialized_ = false;

        int viewport_width_;
        int viewport_height_;
    };
}  // namespace tango_augmented_reality



#endif //CPP_TIC_TAC_TOE_SCENE_H
