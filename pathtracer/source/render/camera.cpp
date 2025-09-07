#include "Camera.h"
#include <cmath>

const float PI = 3.14159265359f;

void Camera::updateCameraVectors() {
    Vec3 newFront;
    newFront.x = cos(yaw * PI / 180.0f) * cos(pitch * PI / 180.0f);
    newFront.y = sin(pitch * PI / 180.0f);
    newFront.z = sin(yaw * PI / 180.0f) * cos(pitch * PI / 180.0f);
    front = normalize(newFront);

    // For right-handed coordinate system (Vulkan)
    right = normalize(cross(worldUp, front));  // worldUp x front gives right
    up = normalize(cross(front, right));       // front x right gives up
}

void Camera::processKeyboard(const std::string& direction, float deltaTime) {
    float velocity = speed * deltaTime;
    if (direction == "FORWARD")  position += front * velocity;
    if (direction == "BACKWARD") position -= front * velocity;
    if (direction == "LEFT")     position -= right * velocity;
    if (direction == "RIGHT")    position += right * velocity;
}

void Camera::processMouse(float xoffset, float yoffset) {
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;

    // Constrain pitch to avoid gimbal lock
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    updateCameraVectors();
}