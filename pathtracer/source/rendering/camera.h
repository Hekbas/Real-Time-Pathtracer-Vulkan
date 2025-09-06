#pragma once
#include "math/vec3.h"
#include "math/mat4.h"
#include <string>

struct Camera {
    Vec3 position = { 0.0f, 99.0f,  0.0f };
    Vec3 front = { 0.0f,  0.0f, -1.0f };
    Vec3 up = { 0.0f, -1.0f,  0.0f };
    Vec3 right = { 1.0f,  0.0f,  0.0f };
    Vec3 worldUp = { 0.0f, -1.0f,  0.0f };

    float yaw = 0.0f;
    float pitch = 0.0f;
    float speed = 50.0f;
    float sensitivity = 0.1f;

    static Mat4 lookAt(const Camera& camera);
    void updateCameraVectors();
    void processKeyboard(const std::string& direction, float deltaTime);
    void processMouse(float xoffset, float yoffset);
};