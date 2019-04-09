QT += core
QT -= gui

CONFIG += c++11

TARGET = VulkanLearn
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES += main.cpp

GLM_DIR = F:/opengl/glm-0.9.9.4/qt5.6/lib-release
GLFW_DIR = F:/opengl/glfw-3.2.1/qt5.6/lib-release
VulKan_LIB_DIR = D:/VulkanSDK/1.1.77.0/Source/lib32
VulKan_DIR = D:/VulkanSDK/1.1.77.0/
Stb_DIR = F:/vulkan/stb
ObjLoader_DIR = F:/vulkan/tinyobjloader

INCLUDEPATH += $${GLM_DIR}/include
INCLUDEPATH += $${GLFW_DIR}/include
INCLUDEPATH += $${VulKan_DIR}/Include
INCLUDEPATH += $${Stb_DIR}/
INCLUDEPATH += $${ObjLoader_DIR}/


LIBS += -L$${GLFW_DIR}/lib/ -lglfw3

LIBS += -L$${VulKan_LIB_DIR}/ \
        libvulkan-1

