#include <QCoreApplication>

//#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>


#include <iostream>
#include <stdexcept>
#include <functional>
#include <cstdlib>

const int WIDTH = 800;
const int HEIGHT = 600;
//校验层的名称
const std::vector<const char*> validataionLayers = {
    "VK_LAYER_LUNARG_standard_validation"
};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif


struct QueueFamilyIndices{
    int graphicsFamily = -1;//表示没有找到满足需求的队列族
    bool isComplete(){
        return graphicsFamily >= 0;
    }
};

class HelloTriangle{
public:
    void run(){
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }
private:
    GLFWwindow* window = nullptr;//窗口
    VkInstance instance;//vulkan实例
    VkDebugUtilsMessengerEXT callback;//存储回调函数信息
    //存储我们使用的显卡信息
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

    ///创建glfw窗口
    void initWindow(){
        glfwInit();//初始化glfw库
        //显示阻止自动创建opengl上下文
        glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
        //禁止窗口大小改变
        glfwWindowHint(GLFW_RESIZABLE,GLFW_FALSE);
        //创建窗口
        window = glfwCreateWindow(WIDTH,HEIGHT,"vulakn",
                                  nullptr,nullptr);
    }
    void createInstance(){
        if(enableValidationLayers && !checkValidationLayerSupport()){
            throw std::runtime_error(
                        "validation layers requested,but not available");
        }
        /**
          设置写应用程序信息，这些信息的填写不是必须的，但填写的信
        息可能会作为驱动程序的优化依据，让驱动程序进行一些特殊的优化。比
        如，应用程序使用了某个引擎，驱动程序对这个引擎有一些特殊处理，这
        时就可能有很大的优化提升：
          */
        VkApplicationInfo appinfo={};
        appinfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appinfo.pApplicationName = "hello";
        appinfo.applicationVersion = VK_MAKE_VERSION(1,1,101);
        appinfo.pEngineName = "No Engine";
        appinfo.engineVersion = VK_MAKE_VERSION(1,1,101);
        appinfo.apiVersion = VK_API_VERSION_1_1;

        //设置vulkan实例信息
        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appinfo;
        /**
          返回支持的扩展列表
        */
        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr,
                                             &extensionCount,nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr,
                                               &extensionCount,
                                               extensions.data());
        std::cout << "available extension:" << std::endl;
        for(const auto& extension : extensions){
            std::cout << "\t"<<extension.extensionName<<std::endl;
        }

        //设置扩展列表
        auto extensions2 = getRequiredExtensions();
        createInfo.enabledExtensionCount =
                static_cast<uint32_t>(extensions2.size());
        createInfo.ppEnabledExtensionNames = extensions2.data();

        if(enableValidationLayers){
            //设置layer信息
            createInfo.enabledLayerCount =
                    static_cast<uint32_t>(validataionLayers.size());
            createInfo.ppEnabledLayerNames = validataionLayers.data();
        }else{

            createInfo.enabledLayerCount = 0;
        }
        //创建vulkan实例
        VkResult result = vkCreateInstance(&createInfo,nullptr,
                                           &instance);
        if(result != VK_SUCCESS){
            std::cout << "vkCreateInstance error:"<<result<<std::endl;
            throw std::runtime_error("failed to create instance!");
        }else{
            std::cout << "vkCreateInstance succeed!!"<<std::endl;
        }
    }
    //创建VkDebugUtilsMessengerEXT
    VkResult CreateDebugUtilsMessengerEXT(VkInstance instance ,
             const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
             const VkAllocationCallbacks * pAllocator,
             VkDebugUtilsMessengerEXT* pCallback){
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                    instance,"vkCreateDebugUtilsMessengerEXT");
        if ( func != nullptr ){
            return func(instance,pCreateInfo,pAllocator,pCallback);
        }else{
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }
    //销毁VkDebugUtilsMessengerEXT
    void DestroyDebugUtilsMessengerEXT(VkInstance instance ,
                                VkDebugUtilsMessengerEXT callback,
                                const VkAllocationCallbacks* pAllocator){
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                    instance,"vkDestroyDebugUtilsMessengerEXT");
        if ( func != nullptr ){
            return func(instance,callback,pAllocator);
        }
    }
    //调试回调
    void setupDebugCallback(){
        if(!enableValidationLayers)
            return;
        VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
        createInfo.sType =
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        //可以用来指定回调函数处理的消息级别
        createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        //指定回调函数处理的消息类型
        createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        //指向回调函数的指针
        createInfo.pfnUserCallback = debugCallback ;
        //指向用户自定义数据的指针它是可选的
        //这个指针所指的地址会被作为回调函数的参数，用来向回调函数传递用户数据
        createInfo.pUserData = nullptr ; // Optional

        if(CreateDebugUtilsMessengerEXT(instance,&createInfo,
                      nullptr,&callback) != VK_SUCCESS){
            throw std::runtime_error("faild to set up debug callback!");
        }
    }
    //检查设备是否满足需求
    bool isDeviceSuitable(VkPhysicalDevice device){
        QueueFamilyIndices indices = findQueueFamilies(device);
        return indices.isComplete();

        //以下代码不使用，仅做了解
        //基础设备属性，名称/类型/支持的vulkan版本
        VkPhysicalDeviceProperties deviceProperties;
        //特征属性，纹理压缩/64位浮点/多视口渲染
        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceProperties(device,&deviceProperties);
        vkGetPhysicalDeviceFeatures(device,&deviceFeatures);
        //显卡是否支持集合着色器
        bool res =
        (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                && deviceFeatures.geometryShader;
        return res;
    }

    //选择一个物理设备
    void pickPhysicalDevice(){
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance,&deviceCount,nullptr);
        if(deviceCount == 0){
            throw std::runtime_error(
                        "failed to find gpus with vulkan support!");
        }
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance,&deviceCount,devices.data());
        for(const auto& device : devices){
            if(isDeviceSuitable(device)){
                physicalDevice = device;
                break;
            }
        }
        if(physicalDevice == VK_NULL_HANDLE){
            throw std::runtime_error("failed to find a suitable gpu!");
        }
    }

    void initVulkan(){
        createInstance();//创建vulkan实例
        setupDebugCallback();//调试回调
        pickPhysicalDevice();//选择一个物理设备
    }
    void mainLoop(){
        //添加事件循环
        while(!glfwWindowShouldClose(window)){
            glfwPollEvents();
        }
    }
    void cleanup(){
        if(enableValidationLayers){
            DestroyDebugUtilsMessengerEXT(instance,callback,nullptr);
        }
        //销毁vulkan实例
        vkDestroyInstance(instance,nullptr);
        //销毁窗口
        glfwDestroyWindow(window);
        //结束glfw
        glfwTerminate();
    }
    bool checkValidationLayerSupport(){//校验层
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount,nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount,
                                           availableLayers.data());
        for(const char* layerName : validataionLayers){
            bool layerFound = false;
            for(const auto& layerProperties : availableLayers){
                std::cout << "layername:"<<layerProperties.layerName<<std::endl;
                if(strcmp(layerName,layerProperties.layerName)==0){
                    layerFound = true;
                    break;
                }
            }
            if(!layerFound){
                return false;
            }
        }
        return true;
    }
    //根据是否启用校验层，返回所需的扩展列表
    std::vector<const char*> getRequiredExtensions(){
        uint32_t glfwExtensionCount =0;
        const char** glfwExtensions;
        glfwExtensions=glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> extensions(glfwExtensions,
                                            glfwExtensions+glfwExtensionCount);
        if(enableValidationLayers){
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        return extensions;
    }
    /**
      第一个参数：
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT：诊断信息
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT：资源创建之类的信息
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT：警告信息
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT：不合法和可能造成崩溃的操作信息
    第二个参数：
    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT：
    发生了一些与规范和性能无关的事件
    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT：
    出现了违反规范的情况或发生了一个可能的错误
    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT：
    进行了可能影响 Vulkan 性能的行为
    第三个参数：包含了下面这些非常重要的成员
    pMessage：一个以 null 结尾的包含调试信息的字符串
    pObjects：存储有和消息相关的 Vulkan 对象句柄的数组
    objectCount：数组中的对象个数
    最后一个参数 pUserData 是一个指向了我们设置回调函数时，传递的数据的指针
      */
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData){
        std::cerr<<"validation layer: "<<pCallbackData->pMessage<<std::endl;
        return VK_FALSE;
    }
    //查找出满足我们需求的队列族
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device){
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device,
                                                 &queueFamilyCount,nullptr);
        /**
        VkQueueFamilyProperties包含队列族的很多信息，
        比如支持的操作类型，该队列族可以创建的队列个数
        */
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device,
                                    &queueFamilyCount,queueFamilies.data());

        QueueFamilyIndices indices;
        int i=0;
        for(const auto& queueFamily : queueFamilies){
            //VK_QUEUE_GRAPHICS_BIT表示支持图形指令
            if(queueFamily.queueCount>0 &&
                    queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT){
                indices.graphicsFamily = i;
            }
            if(indices.isComplete()){
                break;
            }
            i++;
        }

        return indices;
    }
};

int main(int argc, char *argv[])
{
    HelloTriangle hello;
    try{
        hello.run();
    }catch(const std::exception& e){
        std::cerr<<e.what()<<std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*

        libVkLayer_api_dump \
        libVkLayer_core_validation \
        libVkLayer_device_limits \
        libVkLayer_image \
        libVkLayer_object_tracker
*/
