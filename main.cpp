#include <QCoreApplication>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>//GLFW 库会自动包含Vulkan 库的头文件

#include <iostream>
#include <stdexcept>
#include <functional>//用于资源管理
#include <cstdlib>//用来使用 EXITSUCCESS 和 EXIT_FAILURE 宏
#include <set> //使用集合
#include <fstream>//读取文件
#define GLM_FORCE_RADIANS//用来使 glm::rotate这些函数使用弧度作为参数的单位
/**
默认情况下，GLM 库的透视投影矩阵使用 OpenGL 的深度值范围 (-
1.0，1.0)。我们需要定义 GLM_FORCE_DEPTH_ZERO_TO_ONE 宏
来让它使用 Vulkan 的深度值范围 (0.0，1.0)。
  */
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL//启用GLM 库的哈希函数
#include <glm/glm.hpp>//线性代数库
//为了使用glm::rotate之类的矩阵变换函数
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>
//为了使用计时函数,我们将通过计时函数实现每秒旋转 90 度的效果
#include <chrono>
/**
默认情况下stb_image.h文件只定义了函数原型，我们需要在包含tb_image.h 文件前
定义 STB_IMAGE_IMPLEMENTATION 宏，来让它将函数实现包含进来。
  */
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <unordered_map>
//顶点结构体
struct Vertex{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;//纹理坐标
    //返回 Vertex 结构体的顶点数据存放方式
    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription = {};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 3>
                                getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription,3> attributeDescriptions=
                {};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);
        //使用纹理坐标来将纹理映射到几何图元上
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        return attributeDescriptions;
    }
    //重写==运算符
    bool operator==(const Vertex& other) const {
        return pos == other.pos &&
                color == other.color &&
                texCoord == other.texCoord;
    }
};
//对 Vertex 结构体进行哈希的函数
namespace std {
    template<> struct hash<Vertex> {
        size_t operator()(Vertex const& vertex) const {
            return ((hash<glm::vec3>()(vertex.pos) ^
                     (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
                    (hash<glm::vec2>()(vertex.texCoord) << 1);
        }
    };
}
#if 0
//定义顶点数据--交叉顶点属性 (interleaving vertex attributes)。
const std::vector<Vertex> vertices = {
    {{-0.5f ,-0.5f, 0.0f}, {1.0f , 0.0f , 0.0f}, {1.0f,0.0f}},
    {{0.5f , -0.5f, 0.0f}, {0.0f , 1.0f , 0.0f}, {0.0f,0.0f}},
    {{0.5f , 0.5f , 0.0f},  {0.0f , 0.0f , 1.0f}, {0.0f,1.0f}},
    {{-0.5f , 0.5f, 0.0f}, {1.0f , 1.0f , 1.0f}, {1.0f,1.0f}},

    {{-0.5f ,-0.5f,-0.5f}, {1.0f , 0.0f , 0.0f}, {1.0f,0.0f}},
    {{0.5f , -0.5f,-0.5f}, {0.0f , 1.0f , 0.0f}, {0.0f,0.0f}},
    {{0.5f , 0.5f ,-0.5f},  {0.0f , 0.0f , 1.0f}, {0.0f,1.0f}},
    {{-0.5f , 0.5f,-0.5f}, {1.0f , 1.0f , 1.0f}, {1.0f,1.0f}}
};
//定义索引数据
const std::vector<uint16_t> indices = {
    0,1,2,2,3,0,
    4,5,6,6,7,4
};
#endif
//着色器中使用的 uniform 数据
struct UniformBufferObject{
    alignas(16) glm::mat4 model;//模型矩阵
    alignas(16) glm::mat4 view;//视图矩阵
    alignas(16) glm::mat4 proj;//投影矩阵
};

//可以同时并行处理的帧数--12
const int MAX_FRAMES_IN_FLIGHT = 2;

const int WIDTH = 800;
const int HEIGHT = 600;

const std::string MODEL_PATH=
        "E:/workspace/Qt5.6/VulkanLearn/models/chalet.obj";
const std::string TEXTURE_PATH=
        "E:/workspace/Qt5.6/VulkanLearn/models/chalet.jpg";
//指定校验层的名称--代表隐式地开启所有可用的校验层--1
const std::vector<const char*> validataionLayers = {
    "VK_LAYER_LUNARG_standard_validation"
};

//设备扩展列表--检测VK_KHR_swapchain--4
const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

//控制是否启用指定的校验层--1
#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

//读取编译的二进制着色器文件--9
static std::vector<char> readFile(const std::string& filename){
    //ate:从文件尾部开始读取,可以根据读取位置确定文件的大小,分配足够的数组空间来容纳数据
    //binary：以二进制的形式读取文件 (避免进行诸如行末格式是 \n 还是\r\n 的转换)
    std::ifstream file(filename,std::ios::ate | std::ios::binary);
    if(!file.is_open()){
        throw std::runtime_error("failed to open file!");
    }
    //获取文件大小
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(),fileSize);
    file.close();
    return buffer;
}
//表示满足需求得队列族--2
struct QueueFamilyIndices{
    //绘制指令的队列族索引
    int graphicsFamily = -1;//-1表示没有找到满足需求的队列族
    //支持表现的队列族索引
    int presentFamily = -1;
    bool isComplete(){
        return graphicsFamily >= 0 && presentFamily>=0;
    }
};
//查询得到的交换链细节信息：
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class HelloTriangle{
public:
    void run(){
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }
protected:
    static void mouse_button_callback(GLFWwindow* window, int button,
                               int action, int mods){
        auto app =
        reinterpret_cast<HelloTriangle*>(glfwGetWindowUserPointer(window));
        if(action == GLFW_PRESS){
            switch (button) {
            case GLFW_MOUSE_BUTTON_LEFT:
                app->mouseLeftPress = true;
                std::cout << "left button down!"<<std::endl;
                break;
            case GLFW_MOUSE_BUTTON_RIGHT:
                std::cout << "right button down!"<<std::endl;
                break;
            default:
                break;
            }
        }
        if(action == GLFW_RELEASE){
            switch (button) {
            case GLFW_MOUSE_BUTTON_LEFT:
                app->mouseLeftPress = false;
                std::cout << "left button up!"<<std::endl;
                break;
            case GLFW_MOUSE_BUTTON_RIGHT:
                std::cout << "right button up!"<<std::endl;
                break;
            default:
                break;
            }
        }
    }
    static void CursorPos_Callback(GLFWwindow* window, double x, double y){
        auto app =
        reinterpret_cast<HelloTriangle*>(glfwGetWindowUserPointer(window));
        if(app->mouseLeftPress){
            std::cout<<"pos:"<<x<<","<<y<<std::endl;
        }
    }

private:
    bool mouseLeftPress = false;//鼠标左键是否按下
    GLFWwindow* window = nullptr;//窗口句柄--1
    VkInstance instance;//vulkan实例句柄--1
    VkDebugUtilsMessengerEXT callback;//存储回调函数信息--1

    //存储我们使用的显卡信息,物理设备象，可以在 VkInstance 进行清除操作时，自动清除自己--2
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

    VkDevice device;//逻辑设备--3
    VkQueue graphicsQueue;//逻辑设备的队列句柄,自动被清除--3

    //尽管 VkSurfaceKHR 对象是平台无关的，但它的创建依赖窗口系统
    VkSurfaceKHR surface;//窗口表面--4
    VkQueue presentQueue;//呈现队列--4
    VkSwapchainKHR swapChain;//交换链--4
    //交换链的图像句柄,在交换链清除时自动被清除--4
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;//交换链图像格式--4
    VkExtent2D swapChainExtent;//交换链图像范围--4

    //存储图像视图--5
    std::vector<VkImageView> swapChainImageViews;

    VkRenderPass renderPass;//渲染流程对象--9
    /**
     * @brief descriptorSetLayout
     * 描述符布局对象可以在应用程序的整个生命周期使用，即使使用了新的管线对象
     */
    VkDescriptorSetLayout descriptorSetLayout ;//描述符布局对象
    VkPipelineLayout pipelineLayout;//管线布局--9
    VkPipeline graphicsPipeline;//管线对象--9

    //存储所有帧缓冲对象--10
    std::vector<VkFramebuffer> swapChainFramebuffers;

    //指令池对象,管理指令缓冲对象使用的内存，并负责指令缓冲对象的分配--11
    VkCommandPool commandPool ;
    //存储创建的指令缓冲对象,指令缓冲对象会在指令池对象被清除时自动被清除--11
    std::vector<VkCommandBuffer> commandBuffers;

    //为每一帧创建属于它们自己的信号量
    //信号量发出图像已经被获取，可以开始渲染的信号--12
    std::vector<VkSemaphore> imageAvailableSemaphores ;
    //量发出渲染已经结果，可以开始呈现的信号--12
    std::vector<VkSemaphore> renderFinishedSemaphores ;
    //追踪当前渲染的是哪一帧--12
    size_t currentFrame = 0;
    /**
    需要使用栅栏(fence) 来进行 CPU 和 GPU 之间的同步，
    来防止有超过 MAX_FRAMES_IN_FLIGHT帧的指令同时被提交执行。
    栅栏 (fence) 和信号量 (semaphore) 类似，可以用来发出信号和等待信号
      */
    //每一帧创建一个VkFence 栅栏对象--12
    std::vector<VkFence> inFlightFences ;

    //标记窗口大小是否发生改变：
    bool framebufferResized = false;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    VkBuffer vertexBuffer;//存储创建的顶点缓冲的句柄
    VkDeviceMemory vertexBufferMemory ;//顶点缓冲的内存句柄

    VkBuffer indexBuffer ;//存储创建的索引缓冲的句柄
    VkDeviceMemory indexBufferMemory ;//索引缓冲的内存句柄

    std::vector<VkBuffer> uniformBuffers;//uniform 缓冲对象集合
    std::vector<VkDeviceMemory> uniformBuffersMemory;//uniform缓冲对象的内存句柄
    VkDescriptorPool descriptorPool ;//描述符池对象
    std::vector<VkDescriptorSet> descriptorSets;//描述符集对象集合,自动清除
    /**
    尽管，我们可以在着色器直接访问缓冲中的像素数据，但使用 Vulkan的图像对象会更好。
    Vulkan 的图像对象允许我们使用二维坐标来快速获取颜色数据。
    图像对象的像素数据也被叫做纹素
     */
    VkImage textureImage ;//纹理图像
    //细化级别是在创建 VkImage 对象时设置的，之前，我们一直将其设置为 1
    uint32_t mipLevels;//存储计算出的细化级别个数
    VkDeviceMemory textureImageMemory ;//纹理图像对象的内存

    VkImageView textureImageView ;//纹理图像的图像视图对象
    VkSampler textureSampler ;//采样器对象

    /**
    深度附着和颜色附着一样都是基于图像对象。区别是，交换链不会自
    动地为我们创建深度附着使用的深度图像对象。我们需要自己创建深度图
    像对象。使用深度图像需要图像、内存和图像视图对象这三种资源
     */
    VkImage depthImage ;
    VkDeviceMemory depthImageMemory ;
    VkImageView depthImageView ;

    //为静态函数才能将其用作回调函数
    static void framebufferResizeCallback(GLFWwindow* window,int width ,
                                           int height){
        auto app =
        reinterpret_cast<HelloTriangle*>(glfwGetWindowUserPointer(window));
        app->framebufferResized = true;
    }

    ///初始化glfw
    void initWindow(){
        glfwInit();//初始化glfw库
        //显示阻止自动创建opengl上下文
        glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
        //禁止窗口大小改变
        //glfwWindowHint(GLFW_RESIZABLE,GLFW_FALSE);
        /**
        glfwCreateWindow 函数:
        前三个参数指定了要创建的窗口的宽度，高度和标题.
        第四个参数用于指定在哪个显示器上打开窗口,
        最后一个参数与 OpenGL 相关
          */
        //创建窗口
        window = glfwCreateWindow(WIDTH,HEIGHT,"vulakn",
                                  nullptr,nullptr);
        //存储在 GLFW 窗口相关的数据
        glfwSetWindowUserPointer(window , this);
        //窗口大小改变的回调函数
        glfwSetFramebufferSizeCallback(window,framebufferResizeCallback);
        //鼠标事件
        glfwSetMouseButtonCallback(window,mouse_button_callback);
        glfwSetCursorPosCallback(window,CursorPos_Callback);
    }
    //创建VkInstance实例--1
    void createInstance(){
        //是否启用校验层并检测指定的校验层是否支持
        if(enableValidationLayers && !checkValidationLayerSupport()){
            throw std::runtime_error(
                        "validation layers requested,but not available");
        }
        /**
        VkApplicationInfo设置写应用程序信息，这些信息的填写不是必须的，但填写的信息
        可能会作为驱动程序的优化依据，让驱动程序进行一些特殊的优化。比如，应用程序使用了
        某个引擎，驱动程序对这个引擎有一些特殊处理，这时就可能有很大的优化提升。
          */
        /**
          Vulkan 创建对象的一般形式如下:
            sType 成员变量来显式指定结构体类型
            pNext 成员可以指向一个未来可能扩展的参数信息--这个教程里不使用
          */
        VkApplicationInfo appinfo={};
        appinfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appinfo.pApplicationName = "hello";
        appinfo.applicationVersion = VK_MAKE_VERSION(1,1,77);
        appinfo.pEngineName = "No Engine";
        appinfo.engineVersion = VK_MAKE_VERSION(1,1,77);
        appinfo.apiVersion = VK_API_VERSION_1_1;

        /**
        VkInstanceCreateInfo告诉Vulkan的驱动程序需要使用的全局扩展和校验层
        全局是指这里的设置对于整个应用程序都有效，而不仅仅对一个设备有效
          */
        //设置vulkan实例信息
        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appinfo;
        /**
          返回支持的扩展列表:
            我们可以获取扩展的个数，以及扩展的详细信息
        */
        uint32_t extensionCount = 0;//扩展的个数
        vkEnumerateInstanceExtensionProperties(nullptr,
                                             &extensionCount,nullptr);
        //分配数组来存储扩展信息
        //每个 VkExtensionProperties 结构体包含了扩展的名字和版本信息
        std::vector<VkExtensionProperties> extensions(extensionCount);
        //获取所有扩展信息
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

        //判断是否启用校验层,如果启用则设置校验层信息
        if(enableValidationLayers){
            //设置layer信息
            createInfo.enabledLayerCount =
                    static_cast<uint32_t>(validataionLayers.size());
            createInfo.ppEnabledLayerNames = validataionLayers.data();
        }else{

            createInfo.enabledLayerCount = 0;
        }
        /**
          创建 Vulkan 对象的函数参数的一般形式如下：
          1.一个包含了创建信息的结构体指针
          2.一个自定义的分配器回调函数，本教程未使用，设置为nullptr
          3.一个指向新对象句柄存储位置的指针
          */
        //创建vulkan实例
        VkResult result = vkCreateInstance(&createInfo,nullptr,
                                           &instance);
        if(result != VK_SUCCESS){
            throw std::runtime_error("failed to create instance!");
        }
    }
    //使用代理函数创建VkDebugUtilsMessengerEXT--1
    VkResult CreateDebugUtilsMessengerEXT(VkInstance instance ,
             const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
             const VkAllocationCallbacks * pAllocator,
             VkDebugUtilsMessengerEXT* pCallback){
        /**
        vkCreateDebugUtilsMessengerEXT 函数是一个扩展函数，不会被Vulkan库
        自动加载，所以需要我们自己使用 vkGetInstanceProcAddr 函数来加载它

        函数的第二个参数是可选的分配器回调函数，我们没有自定义的分配器，
        所以将其设置为 nullptr。由于我们的调试回调是针对特定Vulkan实例和它的校验层，
        所以需要在第一个参数指定调试回调作用的 Vulkan 实例。
          */
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                    instance,"vkCreateDebugUtilsMessengerEXT");
        if ( func != nullptr ){
            //使用代理函数来创建扩展对象
            return func(instance,pCreateInfo,pAllocator,pCallback);
        }else{
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }
    //创建代理函数销毁VkDebugUtilsMessengerEXT--1
    void DestroyDebugUtilsMessengerEXT(VkInstance instance ,
                                VkDebugUtilsMessengerEXT callback,
                                const VkAllocationCallbacks* pAllocator){
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                    instance,"vkDestroyDebugUtilsMessengerEXT");
        if ( func != nullptr ){
            return func(instance,callback,pAllocator);
        }
    }
    //设置调试回调--1
    void setupDebugCallback(){
        //如果未启用校验层直接返回
        if(!enableValidationLayers)
            return;
        //设置调试结构体所需的信息
        VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
        createInfo.sType =
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        //用来指定回调函数处理的消息级别
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
        //使用代理函数创建 VkDebugUtilsMessengerEXT 对象
        if(CreateDebugUtilsMessengerEXT(instance,&createInfo,
                      nullptr,&callback) != VK_SUCCESS){
            throw std::runtime_error("faild to set up debug callback!");
        }
    }
    //检测交换链是否支持--4
    bool checkDeviceExtensionSupport(VkPhysicalDevice device){
        /**
        实际上，如果设备支持呈现队列，那么它就一定支持交换链。
        但我们最好还是显式地进行交换链扩展的检测，然后显式地启用交换链扩展。
          */
        //枚举设备扩展列表,检测所需的扩展是否存在
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(
                    device,nullptr,&extensionCount,nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(
                    device,nullptr,&extensionCount,availableExtensions.data());
        std::set<std::string> requiredExtensions(deviceExtensions.begin(),
                                                 deviceExtensions.end());
        for(const auto& extension : availableExtensions){
            requiredExtensions.erase(extension.extensionName);
        }
        //如果这个集合中的元素为 0，说明我们所需的扩展全部都被满足
        return requiredExtensions.empty();
    }

    //检查设备是否满足需求--2
    bool isDeviceSuitable(VkPhysicalDevice device){
        QueueFamilyIndices indices = findQueueFamilies(device);
        //检测交换链是否支持
        bool extensionsSupported = checkDeviceExtensionSupport(device);

        //检测交换链的能力是否满足需求,我们只能在验证交换链扩展可用后查询交换链的细节信息
        //我们只需要交换链至少支持一种图像格式和一种支持我们的窗口表面的呈现模式
        bool swapChainAdequate = false;
        if(extensionsSupported){
            SwapChainSupportDetails swapChainSupport =
                    querySwapChainSupport(device);
            swapChainAdequate= !swapChainSupport.formats.empty()
                    && !swapChainSupport.presentModes.empty();
        }
        //判断是否支持各向异性过滤
        VkPhysicalDeviceFeatures supportedFeatures ;
        vkGetPhysicalDeviceFeatures( device , &supportedFeatures );
        return indices.isComplete() && extensionsSupported &&
                swapChainAdequate && supportedFeatures.samplerAnisotropy;

        //以下代码不使用，仅做了解
        //查询基础设备属性，如名称/类型/支持的vulkan版本
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device,&deviceProperties);
        //特征属性，如纹理压缩/64位浮点/多视口渲染(常用于 VR)
        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(device,&deviceFeatures);
        //判断显卡是否支持集合着色器
        bool res =
        (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                && deviceFeatures.geometryShader;
        /**
          除了直接选择第一个满足需求的设备这种方法,一个更好的方法是给每一个满足需求的设备,
          按照特性加权打分，选择分数最高的设备使用.
          此外，也可以显示满足需求的设备列表，让用户自己选择使用的设备
          */
        return res;
    }

    //选择一个物理设备--2
    void pickPhysicalDevice(){
        //首先需要请求显卡的数量
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance,&deviceCount,nullptr);
        if(deviceCount == 0){
            throw std::runtime_error(
                        "failed to find gpus with vulkan support!");
        }
        //分配数组来存储 VkPhysicalDevice 对象
        std::vector<VkPhysicalDevice> devices(deviceCount);
        //获取所有显卡信息
        vkEnumeratePhysicalDevices(instance,&deviceCount,devices.data());
        for(const auto& device : devices){
            //遍历每个物理设备,查看是否满足需求
            if(isDeviceSuitable(device)){
                physicalDevice = device;
                break;
            }
        }
        if(physicalDevice == VK_NULL_HANDLE){
            throw std::runtime_error("failed to find a suitable gpu!");
        }
    }
    //创建一个逻辑设备--3
    void createLogicalDevice(){
        //获取带有图形能力的队列族
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<int> uniqueQueueFamilies = {indices.graphicsFamily,
                                             indices.presentFamily};
        /**
        目前而言，对于每个队列族，驱动程序只允许创建很少数量的队列，但实际上，
        对于每一个队列族，我们很少需要一个以上的队列。
        我们可以在多个线程创建指令缓冲，然后在主线程一次将它们全部提交，降低调用开销。
        Vulkan需要我们赋予队列一个0.0到1.0之间的浮点数作为优先级来控制指令缓冲的执行顺序。
        即使只有一个队列，我们也要显式地赋予队列优先级
         */
        float queuePriority = 1.0f;
        for(int queueFamily : uniqueQueueFamilies){
            //描述了针对一个队列族我们所需的队列数量。
            VkDeviceQueueCreateInfo queueCreateInfo = {};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex=queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }
        //指定应用程序使用的设备特性
        VkPhysicalDeviceFeatures deviceFeatures = {};
        //各向异性过滤实际上是一个非必需的设备特性,这里指定使用
        deviceFeatures.samplerAnisotropy = VK_TRUE;
        //创建逻辑设备相关信息
        VkDeviceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount =
                static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        //启用交换链扩展--4
        createInfo.enabledExtensionCount=
                static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();
        if(enableValidationLayers){
            //以对设备和 Vulkan 实例使用相同地校验层
            createInfo.enabledLayerCount =
                    static_cast<uint32_t>(validataionLayers.size());
            createInfo.ppEnabledLayerNames = validataionLayers.data();
        }else{
            createInfo.enabledLayerCount = 0;
        }
        /**
        vkCreateDevice 函数的参数:
        1.创建的逻辑设备进行交互的物理设备对象
        2.指定的需要使用的队列信息
        3.可选的分配器回调
        4.用来存储返回的逻辑设备对象的内存地址
        逻辑设备并不直接与Vulkan实例交互,所以创建逻辑设备时不需要使用Vulkan实例作为参数
          */
        //创建逻辑设备
        if(vkCreateDevice(physicalDevice,&createInfo,nullptr,
                          &device) != VK_SUCCESS){
            throw std::runtime_error("failed to create logical device!");
        }
        //获取指定队列族的队列句柄
        //它的参数依次是逻辑设备对象,队列族索引,队列索引,用来存储返回的队列句柄的内存地址
        //我们只创建了一个队列，所以，可以直接使用索引 0
        vkGetDeviceQueue(device,indices.graphicsFamily,0,&graphicsQueue);
        vkGetDeviceQueue(device,indices.presentFamily,0,&presentQueue);
    }
    /**
    来查找合适的交换链设置,设置的内容如下：
    1.表面格式 (颜色，深度)
    2.呈现模式 (显示图像到屏幕的条件)
    3.交换范围 (交换链中的图像的分辨率)
      */
    //选择适当的表面格式--4
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(
            const std::vector<VkSurfaceFormatKHR>& availableFormats){
        /**
         * 每一个 VkSurfaceFormatKHR 条目包含了一个 format 和 colorSpace成员变量
         * format 成员变量用于指定颜色通道和存储类型,如
         VK_FORMAT_B8G8R8A8_UNORM 表示我们以B，G，R 和 A 的顺序，
        每个颜色通道用 8 位无符号整型数表示,总共每像素使用 32 位表示
         * olorSpace 成员变量用来表示 SRGB 颜色空间是否被支持，
         * 是否使用 VK_COLOR_SPACE_SRGB_NONLINEAR_KHR 标志
         *
         * 对于颜色空间,如果SRGB被支持,我们就使用SRGB,它可以得到更加准确的颜色表示
         * 这里使用VK_FORMAT_B8G8R8A8_UNORM表示RGB 作为颜色格式
         */
        if(availableFormats.size() < 1){
            throw std::runtime_error("availableFormats size<1");
        }
        //VK_FORMAT_UNDEFINED表明表面没有自己的首选格式
        if(availableFormats.size() == 1 &&
                availableFormats[0].format == VK_FORMAT_UNDEFINED){
            return {VK_FORMAT_B8G8R8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        }
        //检测格式列表中是否有我们想要设定的格式是否存在
        for(const auto& availableFormat : availableFormats){
            if(availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM
                    && availableFormat.colorSpace ==
                    VK_COLOR_SPACE_SRGB_NONLINEAR_KHR){
                return availableFormat;
            }
        }
        /**
        如果不能在列表中找到我们想要的格式，我们可以对列表中存在的格式进行打分，
        选择分数最高的那个作为我们使用的格式，当然，大多数情况下，
        直接使用列表中的第一个格式也是非常不错的选择。
          */
        return availableFormats[0];
    }
    /**
    呈现模式可以说是交换链中最重要的设置。它决定了什么条件下图像才会显示到屏幕。
    Vulkan 提供了四种可用的呈现模式:
    1.VK_PRESENT_MODE_IMMEDIATE_KHR
    应用程序提交的图像会被立即传输到屏幕上，可能会导致撕裂现象
    2.VK_PRESENT_MODE_FIFO_KHR -- 保证一定可用
    交换链变成一个先进先出的队列，每次从队列头部取出一张图像进行显示，
    应用程序渲染的图像提交给交换链后，会被放在队列尾部。
    当队列为满时，应用程序需要进行等待。这一模式非常类似现在常用的垂直同步。
    刷新显示的时刻也被叫做垂直回扫。
    3.VK_PRESENT_MODE_FIFO_RELAXED_KHR
    这一模式和上一模式的唯一区别是，如果应用程序延迟，导致交换链的队列在上一次垂直回扫时为空，
    那么，如果应用程序在下一次垂直回扫前提交图像，图像会立即被显示。
    这一模式可能会导致撕裂现象
    4.VK_PRESENT_MODE_MAILBOX_KHR -- 表现最佳
    这一模式是第二种模式的另一个变种。它不会在交换链的队列满时阻塞应用程序，
    队列中的图像会被直接替换为应用程序新提交的图像。
    这一模式可以用来实现三倍缓冲，避免撕裂现象的同时减小了延迟问题
      */
    //查找最佳的可用呈现模式--4
    VkPresentModeKHR chooseSwapPresentMode(
            const std::vector<VkPresentModeKHR> availablePresentModes){
        VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;
        for(const auto& availablePresentMode : availablePresentModes){
            if(availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR){
                return availablePresentMode;
            }else if(availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR){
                bestMode = availablePresentMode;
            }
        }
        return bestMode;
    }
    /**
    交换范围是交换链中图像的分辨率，它几乎总是和我们要显示图像的窗口的分辨率相同。
    VkSurfaceCapabilitiesKHR 结构体定义了可用的分辨率范围。
    Vulkan 通过 currentExtent 成员变量来告知适合我们窗口的交换范围。
    一些窗口系统会使用一个特殊值，uint32_t 变量类型的最大值，
    表示允许我们自己选择对于窗口最合适的交换范围，
    但我们选择的交换范围需要在 minImageExtent 与 maxImageExtent 的范围内

    代码中 max 和 min 函数用于在允许的范围内选择交换范围的高度值和宽度值，
    需要在源文件中包含 algorithm 头文件才能够使用它们

    窗口大小改变后，我们需要重新设置交换链图像的大小
      */
    //设置交换范围为当前的帧缓冲的实际大小--4
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities){
        if(capabilities.currentExtent.width !=
                std::numeric_limits<uint32_t>::max()){
            return capabilities.currentExtent;
        }else{
            int width,height;
            glfwGetFramebufferSize(window,&width,&height);
            VkExtent2D actualExtent = {width, height};
            actualExtent.width = std::max(capabilities.minImageExtent.width,
                                   std::min(capabilities.maxImageExtent.width,
                                            actualExtent.width));
            actualExtent.height = std::max(capabilities.minImageExtent.height,
                                   std::min(capabilities.maxImageExtent.height,
                                            actualExtent.height));
            return actualExtent;
        }
    }

    //创建窗口表面--4
    void createSurface(){
        //hwnd:窗口句柄   hinstance:进程实例句柄
        //通过glfw创建窗口表面
        VkResult res = glfwCreateWindowSurface(instance,window,nullptr,&surface);
        if(res != VK_SUCCESS){
            throw std::runtime_error("failed to create window surface!");
        }
    }
    //创建交换链--4
    void createSwapChain(){
        SwapChainSupportDetails swapChainSupport =
                querySwapChainSupport(physicalDevice);
        VkSurfaceFormatKHR surfaceFormat =
                chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode =
                chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
        /**
        设置包括交换链中的图像个数，也就是交换链的队列可以容纳的图像个数。
        我们使用交换链支持的最小图像个数 +1 数量的图像来实现三倍缓冲。
        maxImageCount 的值为 0 表明，只要内存可以满足，我们可以使用任意数量的图像。
          */
        uint32_t imageCount = swapChainSupport.capabilities.minImageCount+1;
        if(swapChainSupport.capabilities.maxImageCount>0 &&
                imageCount > swapChainSupport.capabilities.maxImageCount){
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }
        //创建交换对象需要的结构体信息
        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        //指定每个图像所包含的层次.但对于 VR 相关的应用程序来说，会使用更多的层次
        createInfo.imageArrayLayers = 1;
        //指定我们将在图像上进行怎样的操作--这里是作为传输的目的图像
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        /**
        指定在多个队列族使用交换链图像的方式。
        这一设置对于图形队列和呈现队列不是同一个队列的情况有着很大影响。
        我们通过图形队列在交换链图像上进行绘制操作，然后将图像提交给呈现队列来显示。
        有两种控制在多个队列访问图像的方式:
        VK_SHARING_MODE_EXCLUSIVE：一张图像同一时间只能被一个队列族所拥有，
        在另一队列族使用它之前，必须显式地改变图像所有权。这一模式下性能表现最佳。
        VK_SHARING_MODE_CONCURRENT：图像可以在多个队列族间使用，
        不需要显式地改变图像所有权。

        如果图形和呈现不是同一个队列族，我们使用协同模式来避免处理图像所有权问题。
        协同模式需要我们使用 queueFamilyIndexCount 和pQueueFamilyIndices
        来指定共享所有权的队列族。
        如果图形队列族和呈现队列族是同一个队列族 (大部分情况下都是这样)，
        我们就不能使用协同模式，协同模式需要我们指定至少两个不同的队列族。
        */
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        uint32_t queueFamilyIndices[] = {(uint32_t)indices.graphicsFamily,
                                         (uint32_t)indices.presentFamily};
        if(indices.graphicsFamily != indices.presentFamily){
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }else{
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = nullptr;
        }
        /**
        我们可以为交换链中的图像指定一个固定的变换操作
        (需要交换链具有supportedTransforms 特性)，比如顺时针旋转 90 度或是水平翻转。
        如果读者不需要进行任何变换操作，指定使用 currentTransform 变换即可。
        */
        createInfo.preTransform =
                swapChainSupport.capabilities.currentTransform;
        /**
        compositeAlpha
        成员变量用于指定 alpha 通道是否被用来和窗口系统中的其它窗口进行混合操作。
        通常，我们将其设置为 VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR来忽略掉alpha通道。
        */
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;//设置呈现模式
        //为True表示我们不关心被窗口系统中的其它窗口遮挡的像素的颜色
        createInfo.clipped = VK_TRUE;
        /**
        oldSwapchain需要指定它，是因为应用程序在运行过程中交换链可能会失效。
        比如，改变窗口大小后，交换链需要重建，重建时需要之前的交换链

        可以实现原来的交换链仍在使用时重建新的交换链
          */
        createInfo.oldSwapchain = VK_NULL_HANDLE;
        //创建交换链
        if(vkCreateSwapchainKHR(device,&createInfo,nullptr,&swapChain)
                != VK_SUCCESS){
            throw std::runtime_error("failed to create swap chain!");
        }
        /**
        我们在创建交换链时指定了一个minImageCount成员变量来请求最小需要的交换链图像数量。
        Vulkan 的具体实现可能会创建比这个最小交换链图像数量更多的交换链图像，
        我们在这里，我们仍然需要显式地查询交换链图像数量，确保不会出错。
          */
        //获取交换链图像句柄
        vkGetSwapchainImagesKHR(device,swapChain,&imageCount,nullptr);
        swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device,swapChain,&imageCount,
                                swapChainImages.data());
        //存储我们设置的交换链图像格式和范围
        swapChainImageFormat = surfaceFormat.format;
        swapChainExtent = extent;
    }
    //为交换链中的每一个图像建立图像视图--5
    void createImageViews(){
        //分配足够的数组空间来存储图像视图
        swapChainImageViews.resize(swapChainImages.size());
        //遍历所有交换链图像，创建图像视图
        for(uint32_t i=0; i< swapChainImages.size(); i++){
            swapChainImageViews[i] = createImageView(swapChainImages[i],
                                                     swapChainImageFormat,
                                                   VK_IMAGE_ASPECT_COLOR_BIT,1);
        }
    }
    //使用我们读取的着色器字节码数组作为参数来创建 VkShaderModule 对象--9
    VkShaderModule createShaderModule(const std::vector<char>& code){
        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        /**
        指定存储字节码的数组和数组长度
        需要先将存储字节码的数组指针转换为 const uint32_t* 变量类型，
        来匹配结构体中的字节码指针的变量类型。
        我们指定的指针指向的地址应该符合 uint32_t变量类型的内存对齐方式.
        使用的 std::vector，它的默认分配器分配的内存的地址符合这一要求.
        */
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule shaderModule;
        //创建 VkShaderModule对象
        if(vkCreateShaderModule(device,&createInfo,nullptr,
                                &shaderModule) != VK_SUCCESS){
            throw std::runtime_error("failed to create shader module!");
        }
        return shaderModule;
    }

    //创建图形管线--9
    void createGraphicsPipeline(){
        //着色器字节码的读取
        auto vertShaderCode = readFile(
                    "E:/workspace/Qt5.6/VulkanLearn/shaders/vert.spv");
        auto fragShaderCode = readFile(
                    "E:/workspace/Qt5.6/VulkanLearn/shaders/frag.spv");
        //VkShaderModule是一个对着色器字节码的包装
        VkShaderModule vertShaderModule;
        VkShaderModule fragShaderModule;
        vertShaderModule = createShaderModule(vertShaderCode);
        fragShaderModule = createShaderModule(fragShaderCode);
        //VkPipelineShaderStageCreateInfo指定着色器哪一阶段被使用
        //点着色器
        VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
        vertShaderStageInfo.sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        /**
        指定了着色器在管线的哪一阶段被使用。每个可编程阶段都有一个对应这一阶段的枚举值，
        我们使用这一枚举值指定着色器被使用的阶段
          */
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        //指定阶段使用的着色器模块对象
        vertShaderStageInfo.module = vertShaderModule;
        //指定阶段调用的着色器函数
        vertShaderStageInfo.pName = "main";
        //片段着色器
        VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
        fragShaderStageInfo.sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";
        /**
          pSpecializationInfo--指定着色器用到的常量,
        我们可以对同一个着色器模块对象指定不同的着色器常量用于管线创建，这
        使得编译器可以根据指定的着色器常量来消除一些条件分支，这比在渲染
        时，使用变量配置着色器带来的效率要高得多。如果不使用着色器常量，
        可以将 pSpecializationInfo 成员变量设置为 nullptr。
          */
        VkPipelineShaderStageCreateInfo shaderStages [] = {
            vertShaderStageInfo , fragShaderStageInfo
        };

        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        /**
          描述内容主要包括下面两个方面：
        绑定：数据之间的间距和数据是按逐顶点的方式还是按逐实例的方式进行组织
        属性描述：传递给顶点着色器的属性类型，用于将属性绑定到顶点着色器中的变量
        由于我们直接在顶点着色器中硬编码顶点数据,so这里不载入任何顶点数据
          */
        //描述传递给顶点着色器的顶点数据格式--顶点输入
        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.sType =
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount=1;
        //用于指向描述顶点数据组织信息地结构体数组
        vertexInputInfo.pVertexBindingDescriptions=&bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount =
                static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions =
                attributeDescriptions.data();;
        /**
        VkPipelineInputAssemblyStateCreateInfo 结构体用于描述两个信息：
        1.顶点数据定义了哪种类型的几何图元,通过 topology 成员变量指定：如下值
        VK_PRIMITIVE_TOPOLOGY_POINT_LIST：点图元
        VK_PRIMITIVE_TOPOLOGY_LINE_LIST：每两个顶点构成一个线段图元
        VK_PRIMITIVE_TOPOLOGY_LINE_STRIP：每两个顶点构成一个线段图元，
        除第一个线段图元外，每个线段图元使用上一个线段图元的一个顶点
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST：每三个顶点构成一个三角形图元
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP：
        每个三角形的第二个和第三个顶点被下一个三角形作为第一和第二个顶点使用
        2.是否启用几何图元重启

        一般我们会通过索引缓冲来更好地复用顶点缓冲中的顶点数据。
          */
        //输入装配
        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        inputAssembly.sType =
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;
        /**
        需要注意，交换链图像的大小可能与窗口大小不同。交换链图像在之
        后会被用作帧缓冲，所以这里我们设置视口大小为交换链图像的大小。

        视口定义了图像到帧缓冲的映射关系，裁剪矩形定义了哪一区域的像
        素实际被存储在帧缓存。任何位于裁剪矩形外的像素都会被光栅化程序丢弃.
          */
        //视口和裁剪
        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)swapChainExtent.width;
        viewport.height = (float)swapChainExtent.height;
        //用于指定帧缓冲使用的深度值的范围
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        //裁剪范围设置
        //这里我们在整个帧缓冲上进行绘制操作,所以将裁剪范围设置为和帧缓冲大小一样
        VkRect2D scissor = {};
        scissor.offset = {0,0};
        scissor.extent = swapChainExtent;
        /**
        许多显卡可以使用多个视口和裁剪矩形，所以指定视口和裁剪矩形的成员变量
        是一个指向视口和裁剪矩形的结构体数组指针。
        使用多个视口和裁剪矩形需要启用相应的特性支持。
          */
        //将视口和裁剪矩形需要组合在一起
        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType =
                VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;
        /**
        光栅化程序将来自顶点着色器的顶点构成的几何图元转换为片段交由片段着色器着色。
        深度测试，背面剔除和裁剪测试如何开启了，也由光栅化程序执行。
        我们可以配置光栅化程序输出整个几何图元作为片段，
        还是只输出几何图元的边作为片段 (也就是线框模式)。
          */
        //配置光栅化程序
        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        rasterizer.sType =
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        /**
          为True表示在近平面和远平面外的片段会被截断为在近平面和远平面上，
          而不是直接丢弃这些片段,这对于阴影贴图的生成很有用。
          使用这一设置需要开启相应的 GPU 特性
          */
        rasterizer.depthClampEnable = VK_FALSE;
        /**
        为True表示所有几何图元都不能通过光栅化阶段.这一设置会禁止一切片段输出到帧缓冲
          */
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        /**
          指定几何图元生成片段的方式,可以如下：
        VK_POLYGON_MODE_FILL：整个多边形，包括多边形内部都产生片段
        VK_POLYGON_MODE_LINE：只有多边形的边会产生片段
        VK_POLYGON_MODE_POINT：只有多边形的顶点会产生片段
        使用除了 VK_POLYGON_MODE_FILL 外的模式，需要启用相应的 GPU 特性
          */
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        /**
        指定光栅化后的线段宽度，它以线宽所占的片段数目为单位。
        线宽的最大值依赖于硬件，使用大于 1.0f 的线宽，需要启用相应的 GPU 特性
          */
        rasterizer.lineWidth = 1.0f;
        //指定使用的表面剔除类型.我们可以通过它禁用表面剔除,剔除背面,剔除正面,以及剔除双面
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        //指定顺时针的顶点序是正面，还是逆时针的顶点序是正面
        //VK_FRONT_FACE_CLOCKWISE;VK_FRONT_FACE_COUNTER_CLOCKWISE
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        /**
        光栅化程序可以添加一个常量值或是一个基于片段所处线段的斜率得到的变量值到深度值上。
        这对于阴影贴图会很有用，这里我们不使用,so将depthBiasEnable设为false
          */
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f;
        rasterizer.depthBiasClamp = 0.0f;
        rasterizer.depthBiasSlopeFactor = 0.0f;

        //设置深度测试
        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        depthStencil.sType =
                VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        /**
        depthTestEnable 成员变量用于指定是否启用深度测试。
        depthWriteEnable 成员变量用于指定片段通过深度测试后是否写入它的深度值到深度缓冲。
        使用这两个成员变量可以实现透明效果。透明对象的片段的深度值需
        要和之前不透明对象片段的深度值进行比较，但透明对象的片段的深度值不需要写入深度缓冲
          */
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        /**
        depthCompareOp 成员变量用于指定深度测试使用的比较运算。这里
        我们指定深度测试使用小于比较运算，这一设置下，新的片段只有在它的
        深度值小于深度缓冲中的深度值时才会被写入颜色附着
          */
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        /**
        depthBoundsTestEnable、minDepthBounds 和 maxDepthBounds 成员
        变量用于指定可选的深度范围测试。这一测试开启后只有深度值位于指定
        范围内的片段才不会被丢弃。这里我们不使用这一功能
          */
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.minDepthBounds = 0.0f;
        depthStencil.maxDepthBounds = 1.0f;
        /**
        stencilTestEnable、front 和 back 成员变量用于模板测试，在我们的教
        程中没有用到。如果读者想要使用模板测试，需要注意使用包含模板颜色
        通道的图像数据格式
          */
        depthStencil.stencilTestEnable = VK_FALSE;
        depthStencil.front = {};
        depthStencil.back ={};

        /**
        多重采样是一种组合多个不同多边形产生的片段的颜色来决定
        最终的像素颜色的技术，它可以一定程度上减少多边形边缘的走样现象。
        对于一个像素只被一个多边形产生的片段覆盖，只会对覆盖它的这个片段
        执行一次片段着色器，使用多重采样进行反走样的代价要比使用更高的分
        辨率渲染，然后缩小图像达到反走样的代价小得多。使用多重采样需要启
        用相应的 GPU 特性。
          */
        //对多重采样进行配置
        VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType =
                VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        //multisampling.minSampleShading = 1.0f;
        multisampling.pSampleMask = nullptr;//进行深度测试和模板测试
        multisampling.alphaToCoverageEnable = VK_FALSE;
        multisampling.alphaToOneEnable = VK_FALSE;
        /**
        片段着色器返回的片段颜色需要和原来帧缓冲中对应像素的颜色进行混合。
        混合的方式有下面两种：
        1.混合旧值和新值产生最终的颜色
        2.使用位运算组合旧值和新值
        有两个用于配置颜色混合的结构体:
        1.VkPipelineColorBlendAttachmentState --
            对每个绑定的帧缓冲进行单独的颜色混合配置
        2.VkPipelineColorBlendStateCreateInfo --
            进行全局的颜色混合配置
          */
        //颜色混合
        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;
        /**
        blendEnable为false则不会进行混合操作,否则,就会执行指定的混合操作计算新的颜色值.
        计算出的新的颜色值会按照 colorWriteMask 的设置决定写入到帧缓冲的颜色通道
        */
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        //用于设置全局混合常量
        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType =
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        //指定每个帧缓冲的颜色混合设置
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f;
        colorBlending.blendConstants[1] = 0.0f;
        colorBlending.blendConstants[2] = 0.0f;
        colorBlending.blendConstants[3] = 0.0f;
#if 0
        //动态状态
        //只有非常有限的管线状态可以在不重建管线的情况下进行动态修改。
        //这包括视口大小，线宽和混合常量
        VkDynamicState dynamicStates[] ={
            VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_LINE_WIDTH
        };
        //指定需要动态修改的状态
        VkPipelineDynamicStateCreateInfo dynamicState = {};
        dynamicState.sType =
                VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;
#endif
        /**
        我们可以在着色器中使用 uniform 变量，它可以在管线建立后动态地
        被应用程序修改，实现对着色器进行一定程度的动态配置。uniform 变量经
        常被用来传递变换矩阵给顶点着色器，以及传递纹理采样器句柄给片段着色器。
        我们在着色器中使用的uniform变量需要在管线创建时使用VkPipelineLayout对象定义。
        这里暂不使用uniform 变量
          */
        //创建pipelinelayout对象
        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType =
                VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        //设置描述符布局
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pPushConstantRanges = nullptr;
        //VkPipelineLayout 结构体指定可以在着色器中使用的常量值
        if(vkCreatePipelineLayout(device,&pipelineLayoutInfo,nullptr,
                                  &pipelineLayout) != VK_SUCCESS){
            throw std::runtime_error("failed to create pipeline layout!");
        }
        //创建图像管线相关信息
        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType =
                VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        //引用之前我们创建的两个着色器阶段
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        //引用了之前设置的固定功能阶段信息
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        //引用我们刚刚设置的深度模板缓冲状态信息
        //如果渲染流程包含了深度模板附着，那就必须指定深度模板状态信息。
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = nullptr;
        //指定之前创建的管线布局
        pipelineInfo.layout = pipelineLayout;
        //引用之前创建的渲染流程对象和图形管线使用的子流程在子流程数组中的索引
        //在之后的渲染过程中，仍然可以使用其它与这个设置的渲染流程对象相兼容的渲染流程。
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        /**
        basePipelineHandle 和 basePipelineIndex 成员变量用于以一个创建好
        的图形管线为基础创建一个新的图形管线。当要创建一个和已有管线大量
        设置相同的管线时，使用它的代价要比直接创建小，并且，对于从同一个
        管线衍生出的两个管线，在它们之间进行管线切换操作的效率也要高很
        多。我们可以使用 basePipelineHandle 来指定已经创建好的管线，或是使
        用 basePipelineIndex 来指定将要创建的管线作为基础管线，用于衍生新
        的管线。目前，我们只使用一个管线，所以将这两个成员变量分别设置为
        VK_NULL_HANDLE 和 -1，不使用基础管线衍生新的管线。这两个成员
        变量的设置只有在 VkGraphicsPipelineCreateInfo 结构体的 flags 成员变量
        使用了 VK_PIPELINE_CREATE_DERIVATIVE_BIT 标记的情况下才会起效。
          */
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex = -1;
        /**
        这里可以调用可以通过多个 VkGraphicsPipelineCreateInfo
        结构体数据创建多个 VkPipeline 对象.
        第二个参数可以用来引用一个可选的VkPipelineCache 对象。
        通过它可以将管线创建相关的数据进行缓存在多
        个 vkCreateGraphicsPipelines 函数调用中使用，甚至可以将缓存存入文件，
        在多个程序间使用。使用它可以加速之后的管线创建.
          */
        //创建管线对象
        if(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,
                                     &pipelineInfo,nullptr,
                                     &graphicsPipeline) != VK_SUCCESS){
            throw std::runtime_error("failed to create graphics pipeline!");
        }

        vkDestroyShaderModule(device,fragShaderModule,nullptr);
        vkDestroyShaderModule(device,vertShaderModule,nullptr);
    }
    /**
      渲染流程对象包含：
        指定使用的颜色和深度缓冲，以及采样数，渲染操作如何处理缓冲的内容
      */
    //创建渲染流程对象--9
    void createRenderPass(){
        //设置深度附着信息
        VkAttachmentDescription depthAttachment = {};
        //format成员变量的值的设置应该和深度图像的图像数据格式相同
        depthAttachment.format = findDepthFormat();
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        //绘制结束后不需要从深度缓冲中复制深度数据,所以设备为此值,
        //这样可以让驱动进行一定程度的优化
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        //不需要读取之前深度图像数据,所以设备为此值
        depthAttachment.finalLayout =
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference depthAttachmentRef = {};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout =
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


        //代表交换链图像的颜色缓冲附着
        VkAttachmentDescription colorAttachment = {};
        //format指定颜色缓冲附着的格式
        colorAttachment.format = swapChainImageFormat;
        //samples指定采样数,这里我们没有使用多重采样，所以将采样数设置为 1。
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        /**
        loadOp 和 storeOp 成员变量用于指定在渲染之前和渲染之后对附着中的数据进行的操作.
        loadOp可以设置为下面这些值：
        VK_ATTACHMENT_LOAD_OP_LOAD：保持附着的现有内容
        VK_ATTACHMENT_LOAD_OP_CLEAR：使用一个常量值来清除附着的内容
        VK_ATTACHMENT_LOAD_OP_DONT_CARE：不关心附着现存的内容
        storeOp 可以设置为下面这些值：
        VK_ATTACHMENT_STORE_OP_STORE：渲染的内容会被存储起来，以便之后读取
        VK_ATTACHMENT_STORE_OP_DONT_CARE：渲染后，不会读取帧缓冲的内容

        loadOp 和 storeOp 成员变量的设置会对颜色和深度缓冲起效
          */
        //每次渲染新的一帧前使用黑色清除帧缓冲
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        //stencilLoadOp成员变量和stencilStoreOp成员变量会对模板缓冲起效,这里未使用
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        /**
        Vulkan 中的纹理和帧缓冲由特定像素格式的 VkImage 对象来表示。
        图像的像素数据在内存中的分布取决于我们要对图像进行的操作.
        下面是一些常用的图形内存布局设置：
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL：图像被用作颜色附着
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR：图像被用在交换链中进行呈现操作
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL：图像被用作复制操作的目的图像
          */
        //指定渲染流程开始前的图像布局方式,这里了表示不关心之前的图像布局方式
        //使用这一值后,图像的内容不保证会被保留,但对于我们的应用程序,每次渲染前都要清除图像
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        //于指定渲染流程结束后的图像布局方式
        //这里设置使得渲染后的图像可以被交换链呈现。
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        /**
        一个渲染流程可以包含多个子流程。子流程依赖于上一流程处理后的
        帧缓冲内容。比如，许多叠加的后期处理效果就是在上一次的处理结果上
        进行的。我们将多个子流程组成一个渲染流程后，Vulkan 可以对其进行一
        定程度的优化。对于我们这个渲染三角形的程序，我们只使用了一个子流程.

        每个子流程可以引用一个或多个附着
          */
        //指定引用的附着
        VkAttachmentReference colorAttachmentRef = {};
        //指定要引用的附着在附着描述结构体数组中的索引
        colorAttachmentRef.attachment = 0;
        /**
        指定进行子流程时引用的附着使用的布局方式，Vulkan 会在子流程开始时自动将引用
        的附着转换到 layout 成员变量指定的图像布局.
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL性能表现最佳
        */
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        //描述子流程
        VkSubpassDescription subpass = {};
        //显式地指定这是一个图形渲染的子流程
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        /**
        这里设置的颜色附着在数组中的索引会被片段着色器使用，对应我们
        在片段着色器中使用的 layout(location = 0) out vec4 outColor 语句。
        下面是其它一些可以被子流程引用的附着类型:
        pInputAttachments：被着色器读取的附着
        pResolveAttachments：用于多重采样的颜色附着
        pDepthStencilAttachment：用于深度和模板数据的附着
        pPreserveAttachments：没有被这一子流程使用，但需要保留数据的附着
          */
        //们指定引用的颜色附着个数和地址
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        //为我们仅有的子流程添加对深度附着的引用
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        //配置子流程依赖
        VkSubpassDependency dependency = {};
        /**
        srcSubpass 和 dstSubpass 成员变量用于指定被依赖的子流程的索引和
        依赖被依赖的子流程的索引。VK_SUBPASS_EXTERNAL 用来指定我们
        之前提到的隐含的子流程，对 srcSubpass 成员变量使用表示渲染流程开始
        前的子流程，对 dstSubpass 成员使用表示渲染流程结束后的子流程。这里
        使用的索引 0 是我们之前创建的子流程的索引。为了避免出现循环依赖，
        我们给 dstSubpass 设置的值必须始终大于 srcSubpass。
          */
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        /**
        srcStageMask 和 srcAccessMask 成员变量用于指定需要等待的管线阶
        段和子流程将进行的操作类型。我们需要等待交换链结束对图像的读取才
        能对图像进行访问操作，也就是等待颜色附着输出这一管线阶段。
          */
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        /**
        dstStageMask 和 dstAccessMask 成员变量用于指定需要等待的管线阶
        段和子流程将进行的操作类型。在这里，我们的设置为等待颜色附着的输
        出阶段，子流程将会进行颜色附着的读写操作。这样设置后，图像布局变
        换直到必要时才会进行：当我们开始写入颜色数据时。
          */
        dependency.dstStageMask =
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
                | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        /**
        和颜色附着不同，一个子流程只可以使用一个深度 (或深度模板) 附着。
        一般而言，也很少有需要多个深度附着的情况
          */
        //更新 VkRenderPassCreateInfo 结构体信息引用深度附着
        std::array<VkAttachmentDescription,2> attachments = {
                            colorAttachment, depthAttachment};
        //创建渲染流程对象相关信息
        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType =
                VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount =
                static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        //指定渲染流程使用的依赖信息
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;
        //创建渲染流程对象
        if(vkCreateRenderPass(device,&renderPassInfo,nullptr,
                              &renderPass) != VK_SUCCESS){
            throw std::runtime_error("failed to create render pass!");
        }

    }
    //帧缓冲对象的创建--10
    void createFramebuffers(){
        //分配足够的空间来存储所有帧缓冲对象
        swapChainFramebuffers.resize(swapChainImageViews.size());
        for(size_t i = 0;i<swapChainImageViews.size();i++){
            //为交换链的每一个图像视图对象创建对应的帧缓冲
            /**
            和每个交换链图像对应不同的颜色附着不同，使用我们这里的信号量
            设置，同时只会有一个子流程在执行，所以，这里我们只需要使用一个深度附着即可
              */
            //指定深度图像视图对象作为帧缓冲的第二个附着
            std::array<VkImageView,2> attachments = {
                swapChainImageViews[i],depthImageView
            };
            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType =
                    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            /**
            指定帧缓冲需要兼容的渲染流程对象.
            之后的渲染操作，我们可以使用与这个指定的渲染流程对象相兼容的其它渲染流程对象.
            一般来说，使用相同数量，相同类型附着的渲染流程对象是相兼容的。
              */
            framebufferInfo.renderPass = renderPass;
            //指定附着个数
            framebufferInfo.attachmentCount =
                    static_cast<uint32_t>(attachments.size());
            //指定渲染流程对象用于描述附着信息的 pAttachment 数组
            framebufferInfo.pAttachments = attachments.data();
            //指定帧缓冲的大小
            framebufferInfo.width = swapChainExtent.width;
            framebufferInfo.height = swapChainExtent.height;
            //指定图像层数,这里的交换链图像都是单层的
            framebufferInfo.layers = 1;
            //创建帧缓冲对象
            if(vkCreateFramebuffer(device,&framebufferInfo,nullptr,
                                   &swapChainFramebuffers[i]) != VK_SUCCESS){
                throw std::runtime_error("failed to create framebuffer!");
            }
        }
    }
    //创建指令池--11
    void createCommandPool(){
        /**
        指令缓冲对象在被提交给我们之前获取的队列后，被 Vulkan 执行。每
        个指令池对象分配的指令缓冲对象只能提交给一个特定类型的队列。在这
        里，我们使用的是绘制指令，它可以被提交给支持图形操作的队列。

        有下面两种用于指令池对象创建的标记，
        可以提供有用的信息给Vulkan的驱动程序进行一定优化处理：
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT：
        使用它分配的指令缓冲对象被频繁用来记录新的指令
        (使用这一标记可能会改变帧缓冲对象的内存分配策略)
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT：
        指令缓冲对象之间相互独立，不会被一起重置。
        不使用这一标记，指令缓冲对象会被放在一起重置

        这里我们只在程序初始化时记录指令到指令缓冲对象，
        然后在程序的主循环中执行指令，所以，我们不使用上面这两个标记
         */
        QueueFamilyIndices queueFamilyIndices =
                findQueueFamilies(physicalDevice);
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
        poolInfo.flags = 0;
        //指令池对象的创建
        if(vkCreateCommandPool(device,&poolInfo,nullptr,
                               &commandPool) != VK_SUCCESS){
            throw std::runtime_error("failed to create command pool!");
        }
    }
    /**
    指令缓冲对象，用来记录绘制指令
    由于绘制操作是在帧缓冲上进行的，我们需要为交换链中的每一个图像分配一个指令缓冲对象
      */
    //创建指令缓冲对象--11
    void createCommandBuffers(){
        std::array<VkClearValue,2> clearValues = {};
        clearValues[0].color = {0.0f , 0.0f , 0.0f , 1.0f};
        //深度缓冲的初始值应该设置为远平面的深度值,也就是1.0
        clearValues[1].depthStencil = {1.0f, 0};
        commandBuffers.resize(swapChainFramebuffers.size());
        //指定分配使用的指令池和需要分配的指令缓冲对象个数
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType =
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        /**
        level 成员变量用于指定分配的指令缓冲对象是主要指令缓冲对象还是辅助指令缓冲对象:
        VK_COMMAND_BUFFER_LEVEL_PRIMARY：
        可以被提交到队列进行执行，但不能被其它指令缓冲对象调用。
        VK_COMMAND_BUFFER_LEVEL_SECONDARY：
        不能直接被提交到队列进行执行，但可以被主要指令缓冲对象调用执行

        在这里，我们没有使用辅助指令缓冲对象，但辅助治理给缓冲对象的
        好处是显而易见的，我们可以把一些常用的指令存储在辅助指令缓冲对象，
        然后在主要指令缓冲对象中调用执行
          */
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();
        //分配指令缓冲对象
        if(vkAllocateCommandBuffers(device,&allocInfo,
                                    commandBuffers.data())!= VK_SUCCESS){
            throw std::runtime_error("failed to allocate command buffers!");
        }
        //记录指令到指令缓冲
        for(size_t i=0;i<commandBuffers.size();i++){
            //指定一些有关指令缓冲的使用细节
            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType =
                    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            /**
            flags 成员变量用于指定我们将要怎样使用指令缓冲。它的值可以是下面这些:
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT：
            指令缓冲在执行一次后，就被用来记录新的指令.
            VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT：
            这是一个只在一个渲染流程内使用的辅助指令缓冲.
            VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT：
            在指令缓冲等待执行时，仍然可以提交这一指令缓冲
              */
            beginInfo.flags =
                    VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            //用于辅助指令缓冲，可以用它来指定从调用它的主要指令缓冲继承的状态
            beginInfo.pInheritanceInfo = nullptr;
            //指令缓冲对象记录指令后，调用vkBeginCommandBuffer函数会重置指令缓冲对象
            //开始指令缓冲的记录操作
            if(vkBeginCommandBuffer(commandBuffers[i],&beginInfo)!=VK_SUCCESS){
                throw std::runtime_error(
                            "failed to begin recording command buffer.");
            }
            //指定使用的渲染流程对象
            VkRenderPassBeginInfo renderPassInfo = {};
            renderPassInfo.sType =VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            //指定使用的渲染流程对象
            renderPassInfo.renderPass = renderPass;
            //指定使用的帧缓冲对象
            renderPassInfo.framebuffer = swapChainFramebuffers[i];
            /**
            renderArea指定用于渲染的区域。位于这一区域外的像素数据会处于未定义状态。
            通常，我们将这一区域设置为和我们使用的附着大小完全一样.
              */
            renderPassInfo.renderArea.offset = {0,0};
            renderPassInfo.renderArea.extent = swapChainExtent;
            VkClearValue clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
            //指定标记后，使用的清除值
            /**
            我们使用了多个使用 VK_ATTACHMENT_LOAD_OP_CLEAR
            标记的附着，这也意味着我们需要设置多个清除值
              */
            renderPassInfo.clearValueCount =
                    static_cast<uint32_t>(clearValues.size());
            renderPassInfo.pClearValues = clearValues.data();
            /**
            所有可以记录指令到指令缓冲的函数的函数名都带有一个 vkCmd 前缀，
            并且这些函数的返回值都是 void，也就是说在指令记录操作完全结束前，
            不用进行任何错误处理。
            这类函数的第一个参数是用于记录指令的指令缓冲对象。第二个参数
            是使用的渲染流程的信息。最后一个参数是用来指定渲染流程如何提供绘
            制指令的标记，它可以是下面这两个值之一：
            VK_SUBPASS_CONTENTS_INLINE：
            所有要执行的指令都在主要指令缓冲中，没有辅助指令缓冲需要执行
            VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS：
            有来自辅助指令缓冲的指令需要执行。
              */
            //开始一个渲染流程
            vkCmdBeginRenderPass( commandBuffers[i], &renderPassInfo,
                                   VK_SUBPASS_CONTENTS_INLINE) ;
            //绑定图形管线,第二个参数用于指定管线对象是图形管线还是计算管线
            vkCmdBindPipeline(commandBuffers[i],VK_PIPELINE_BIND_POINT_GRAPHICS,
                              graphicsPipeline ) ;

            /**
            至此，我们已经提交了需要图形管线执行的指令，以及片段着色器使用的附着
            */
            VkBuffer vertexBuffers[] = { vertexBuffer };
            VkDeviceSize offset[] = {0};
            /**
            vkCmdBindVertexBuffers第二,三个参数指定偏移值和我们要绑定的顶点缓冲的数量。
            最后两个参数用于指定需要绑定的顶点缓冲数组以及顶点数据在顶点缓冲中的偏移值数组
              */
            //绑定顶点缓冲
            vkCmdBindVertexBuffers(commandBuffers[i],0,1,vertexBuffers,offset);

            /**
              只能绑定一个索引缓冲对象.
            我们不能为每个顶点属性使用不同的索引，所以即使只有一个顶点属性不同，
            也要在顶点缓冲中多出一个顶点的数据
              */
            //绑定顶点缓冲到指令缓冲对象--第三个参数为索引数据的类型
            vkCmdBindIndexBuffer(commandBuffers[i],indexBuffer,0,
                                 VK_INDEX_TYPE_UINT32);
            //为每个交换链图像绑定对应的描述符集
            vkCmdBindDescriptorSets(commandBuffers[i] ,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout,0,1,&descriptorSets[i],0,nullptr);
            /**
              vkCmdDraw参数：
              1.记录有要执行的指令的指令缓冲对象
              2. vertexCount：顶点缓冲中的顶点个数
                尽管这里我们没有使用顶点缓冲，但仍然需要指定三个顶点用于三角形的绘制。
              3.instanceCount：用于实例渲染，为 1 时表示不进行实例渲染
              4.firstVertex：用于定义着色器变量 gl_VertexIndex 的值
              5.firstInstance：用于定义着色器变量 gl_InstanceIndex 的值
              */
            //开始调用指令进行三角形的绘制操作--使用顶点绘制
            /*vkCmdDraw( commandBuffers [ i ] ,
                       static_cast<uint32_t>(vertices.size()) , 1 , 0 , 0);*/
            /**
              vkCmdDrawIndexed参数：
              1.指令缓冲对象
              2.指定索引的个数
              3.实例的个数 -- 这里没有使用实例渲染，所以将实例个数设置为 1
              4.偏移值用于指定显卡开始读取索引的位置,偏移值为1对应索引数据中的第二个索引。
              5.检索顶点数据前加到顶点索引上的数值
              6.第一个被渲染的实例的 ID -- 这里没有使用
              */
            //使用索引绘制
            vkCmdDrawIndexed(commandBuffers[i],
                       static_cast<uint32_t>(indices.size()),1,0,0,0);

            //结束渲染流程
            vkCmdEndRenderPass( commandBuffers [ i ] ) ;
            //结束记录指令到指令缓冲
            if(vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS){
                throw std::runtime_error("failed to record command buffer!");
            }
        }
    }
    //创建信号量和VkFence--12
    void createSyncObjects(){
        //创建每一帧需要的信号量对象
        imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT) ;
        renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT) ;
        inFlightFences.resize(MAX_FRAMES_IN_FLIGHT) ;

        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType =
                VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        /**
        默认情况下，栅栏 (fence) 对象在创建后是未发出信号的状态。
        这就意味着如果我们没有在 vkWaitForFences 函数调用
        之前发出栅栏 (fence) 信号，vkWaitForFences 函数调用将会一直处于等待状态。

        这里设置初始状态为已发出信号
          */
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for( size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++){
            //创建信号量和VkFence 对象
            if(vkCreateSemaphore(device,&semaphoreInfo,nullptr,
                                 &imageAvailableSemaphores[i]) != VK_SUCCESS ||
               vkCreateSemaphore(device,&semaphoreInfo,nullptr,
                                 &renderFinishedSemaphores[i]) != VK_SUCCESS ||
               vkCreateFence(device,&fenceInfo,nullptr,
                             &inFlightFences[i]) != VK_SUCCESS){
                throw std::runtime_error(
                 "failed to create synchronization objects for a frame!");
            }

        }
    }
    //初始化 Vulkan 对象。
    void initVulkan(){
        createInstance();//创建vulkan实例
        setupDebugCallback();//调试回调
        createSurface();//创建窗口表面
        pickPhysicalDevice();//选择一个物理设备
        createLogicalDevice();//创建逻辑设备
        createSwapChain();//创建交换链
        createImageViews();//为交换链中的每一个图像建立图像视图
        createRenderPass();
        createDescriptorSetLayout();//提供着色器使用的每一个描述符绑定信息
        createGraphicsPipeline();//创建图形管线
        createCommandPool();//创建指令池
        createDepthResources();//创建深度图像相关的对象
        createFramebuffers();
        createTextureImage();//加载图像数据到一个Vulkan 图像对象
        createTextureImageView();//创建纹理图像的图像视图对象
        createTextureSampler();//创建采样器对象
        loadModel();//载入模型文件
        createVertexBuffer();//创建顶点缓冲
        createIndexBuffer();//创建索引缓冲
        createUniformBuffer();//创建uniform 缓冲对象
        createDescriptorPool();//描述符池的创建
        createDescriptorSets();//创建描述符集对象
        createCommandBuffers();
        createSyncObjects();
    }
    /**
     * @brief drawFrame
     * 1.从交换链获取一张图像
     * 2.对帧缓冲附着执行指令缓冲中的渲染指令
     * 3.返回渲染后的图像到交换链进行呈现操作
    上面这些操作每一个都是通过一个函数调用设置的, 但每个操作的实际
    执行却是异步进行的。函数调用会在操作实际结束前返回，并且操作的实
    际执行顺序也是不确定的。而我们需要操作的执行能按照一定的顺序，所
    以就需要进行同步操作。
    有两种用于同步交换链事件的方式：栅栏 (fence) 和信号量 (semaphore)。
    它们都可以完成同步操作。
    栅栏 (fence) 和信号量 (semaphore) 的不同之处是，我们可以通过调
    用 vkWaitForFences 函数查询栅栏 (fence) 的状态，但不能查询信号量
    (semaphore) 的状态。通常，我们使用栅栏 (fence) 来对应用程序本身和渲
    染操作进行同步。使用信号量 (semaphore) 来对一个指令队列内的操作或
    多个不同指令队列的操作进行同步。这里，我们想要通过指令队列中的绘
    制操作和呈现操作，显然，使用信号量 (semaphore) 更加合适。
     */
    //绘制图像--12
    void drawFrame(){
        /**
        vkWaitForFences 函数可以用来等待一组栅栏 (fence) 中的一个或
        全部栅栏 (fence) 发出信号。上面代码中我们对它使用的 VK_TRUE
        参数用来指定它等待所有在数组中指定的栅栏 (fence)。我们现在只有
        一个栅栏 (fence) 需要等待，所以不使用 VK_TRUE 也是可以的。和
        vkAcquireNextImageKHR 函数一样，vkWaitForFences 函数也有一个超时
        参数。和信号量不同，等待栅栏发出信号后，我们需要调用 vkResetFences
        函数手动将栅栏 (fence) 重置为未发出信号的状态。
          */
        //等待我们当前帧所使用的指令缓冲结束执行
        vkWaitForFences(device,1,&inFlightFences[currentFrame],
                        VK_TRUE,std::numeric_limits<uint64_t>::max());
        vkResetFences(device , 1 , &inFlightFences[currentFrame]);

        uint32_t imageIndex;
        /**
          vkAcquireNextImageKHR参数：
          1.使用的逻辑设备对象
          2.我们要获取图像的交换链，
          3.图像获取的超时时间，我们可以通过使用无符号 64 位整型所能表示的
            最大整数来禁用图像获取超时
          4,5.指定图像可用后通知的同步对象.可以指定一个信号量对象或栅栏对象，
            或是同时指定信号量和栅栏对象进行同步操作。
            在这里，我们指定了一个叫做 imageAvailableSemaphore 的信号量对象
          6.用于输出可用的交换链图像的索引，我们使用这个索引来引用我们的
            swapChainImages数组中的VkImage对象，并使用这一索引来提交对应的指令缓冲
          */
        //从交换链获取一张图像
        //交换链是一个扩展特性，所以与它相关的操作都会有 KHR 这一扩展后缀
        VkResult result =vkAcquireNextImageKHR(device,swapChain,
                              std::numeric_limits<uint64_t>::max(),
                              imageAvailableSemaphores[currentFrame],
                              VK_NULL_HANDLE,&imageIndex);
        if(result == VK_ERROR_OUT_OF_DATE_KHR){
            recreateSwapChain();
            return;
        }else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR){
            throw std::runtime_error("failed to acquire swap chain image!");
        }

        updateUniformBuffer(imageIndex);//更新 uniform 数据

        //提交信息给指令队列
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
        //这里需要写入颜色数据到图像,所以我们指定等待图像管线到达可以写入颜色附着的管线阶段
        VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        /**
        waitSemaphoreCount、pWaitSemaphores 和pWaitDstStageMask 成员变量用于指定
        队列开始执行前需要等待的信号量，以及需要等待的管线阶段
          */
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        //waitStages 数组中的条目和 pWaitSemaphores 中相同索引的信号量相对应。
        submitInfo.pWaitDstStageMask = waitStages;
        //指定实际被提交执行的指令缓冲对象
        //我们应该提交和我们刚刚获取的交换链图像相对应的指令缓冲对象
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[imageIndex];
        VkSemaphore signalSemaphores [ ] = {
            renderFinishedSemaphores[currentFrame]};
        //指定在指令缓冲执行结束后发出信号的信号量对象
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;
        /**
        vkQueueSubmit 函数使用vkQueueSubmit结构体数组作为参数,可以同时大批量提交数.。
        vkQueueSubmit 函数的最后一个参数是一个可选的栅栏对象，
        可以用它同步提交的指令缓冲执行结束后要进行的操作。
        在这里，我们使用信号量进行同步，没有使用它，将其设置为VK_NULL_HANDLE
          */
        /**
         * @brief vkResetFences
         * 在重建交换链时，重置栅栏 (fence) 对象,
         * 以防导致我们使用的栅栏 (fence) 处于我们不能确定得状态
         */
        vkResetFences(device,1,&inFlightFences[currentFrame]);
        //提交指令缓冲给图形指令队列
        if(vkQueueSubmit(graphicsQueue,1,&submitInfo,
                         inFlightFences[currentFrame])!= VK_SUCCESS){
            throw std::runtime_error("failed to submit draw command buffer!");
        }

        /**
         * 将渲染的图像返回给交换链进行呈现操作
         */
        //配置呈现信息
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        //指定开始呈现操作需要等待的信号量
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        //指定了用于呈现图像的交换链，以及需要呈现的图像在交换链中的索引
        VkSwapchainKHR swapChains [ ] = {swapChain };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;
        /**
        我们可以通过 pResults 成员变量获取每个交换链的呈现操作是否成功
        的信息。在这里，由于我们只使用了一个交换链，可以直接使用呈现函数
        的返回值来判断呈现操作是否成功
          */
        presentInfo.pResults = nullptr;
        /**
          vkQueuePresentKHR函数返回的信息来判定交换链是否需要重建:
            VK_ERROR_OUT_OF_DATE_KHR：交换链不能继续使用。通常发生在窗口大小改变后
            VK_SUBOPTIMAL_KHR：交换链仍然可以使用，但表面属性已经不能准确匹配
          */
        //请求交换链进行图像呈现操作
        result = vkQueuePresentKHR( presentQueue , &presentInfo ) ;
        if(result == VK_ERROR_OUT_OF_DATE_KHR ||
                result == VK_SUBOPTIMAL_KHR || framebufferResized){
            //交换链不完全匹配时也重建交换链
            framebufferResized = false;
            recreateSwapChain();
        }else if(result != VK_SUCCESS){
            throw std::runtime_error("failed to present swap chain image!");
        }
        /**
        如果开启校验层后运行程序，观察应用程序的内存使用情况，
        可以发现我们的应用程序的内存使用量一直在慢慢增加。这是由于我
        们的 drawFrame 函数以很快地速度提交指令，但却没有在下一次指令
        提交时检查上一次提交的指令是否已经执行结束。也就是说 CPU 提交
        指令快过 GPU 对指令的处理速度，造成 GPU 需要处理的指令大量堆
        积。更糟糕的是这种情况下，我们实际上对多个帧同时使用了相同的
        imageAvailableSemaphore 和 renderFinishedSemaphore 信号量。
        最简单的解决上面这一问题的方法是使用 vkQueueWaitIdle 函数来等
        待上一次提交的指令结束执行，再提交下一帧的指令：
        但这样做，是对 GPU 计算资源的大大浪费。图形管线可能大部分时
        间都处于空闲状态.
          */
        //等待一个特定指令队列结束执行
        //vkQueueWaitIdle ( presentQueue ) ;

        //更新currentFrame
        currentFrame = (currentFrame+1) %MAX_FRAMES_IN_FLIGHT;
    }
    //设置主循环
    void mainLoop(){
        //添加事件循环
        //glfwWindowShouldClose检测窗口是否关闭
        while(!glfwWindowShouldClose(window)){
            glfwPollEvents();//执行事件处理
            /**
              drawFrame 函数中的操作是异步执行的:
            这意味着我们关闭应用程序窗口跳出主循环时，绘制操作和呈现操作可能仍在
            继续执行，这与我们紧接着进行的清除操作也是冲突的.
             */
            drawFrame();
        }
        //等待逻辑设备的操作结束执行才能销毁窗口
        vkDeviceWaitIdle(device);
    }
    //清理资源
    void cleanup(){
        cleanupSwapChain();//释放交换链相关
        //清除采样器对象
        vkDestroySampler(device,textureSampler,nullptr);
        //清除纹理图像的图像视图对象，
        vkDestroyImageView ( device , textureImageView , nullptr ) ;
        //销毁纹理对象
        vkDestroyImage(device,textureImage,nullptr);
        //释放纹理对象内存
        vkFreeMemory(device,textureImageMemory,nullptr);
        //销毁描述符池对象
        vkDestroyDescriptorPool(device,descriptorPool,nullptr);
        //销毁描述符对象
        vkDestroyDescriptorSetLayout(device,descriptorSetLayout,nullptr);
        //释放uniform 缓冲对象
        for(size_t i=0;i<swapChainImages.size();i++){
            vkDestroyBuffer(device,uniformBuffers[i],nullptr);
            vkFreeMemory(device,uniformBuffersMemory[i],nullptr);
        }
        //销毁顶点缓冲
        vkDestroyBuffer(device,vertexBuffer,nullptr);
        //释放顶点缓冲缓冲的内存
        vkFreeMemory(device,vertexBufferMemory,nullptr);
        //销毁索引缓冲
        vkDestroyBuffer(device,indexBuffer,nullptr);
        //释放索引缓冲缓冲的内存
        vkFreeMemory(device,indexBufferMemory,nullptr);

        //清除为每一帧创建的信号量和VkFence 对象--12
        for(size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++){
            //所有它所同步的指令执行结束后，对它进行清除
            vkDestroySemaphore(device,renderFinishedSemaphores[i],nullptr);
            vkDestroySemaphore(device,imageAvailableSemaphores[i],nullptr);
            vkDestroyFence(device ,inFlightFences[i], nullptr);
        }
        //销毁指令池对象--11
        vkDestroyCommandPool(device,commandPool,nullptr);

        vkDestroyDevice(device,nullptr);//销毁逻辑设备对象--3
        if(enableValidationLayers){
            //调用代理销毁VkDebugUtilsMessengerEXT对象--1
            DestroyDebugUtilsMessengerEXT(instance,callback,nullptr);
        }
        //销毁窗口表面对象，表面对象的清除需要在 Vulkan 实例被清除之前完成
        vkDestroySurfaceKHR(instance, surface, nullptr);
        /**
        Vulkan 中创建和销毁对象的函数都有一个 VkAllocationCallbacks 参数,
        可以被用来自定义内存分配器,本教程也不使用
         */
        //销毁vulkan实例--1
        vkDestroyInstance(instance,nullptr);
        //销毁窗口--1
        glfwDestroyWindow(window);
        //结束glfw--1
        glfwTerminate();
    }
    //请求所有可用的校验层--1
    bool checkValidationLayerSupport(){
        //vkEnumerateInstanceLayerProperties获取了所有可用的校验层列
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
    //根据是否启用校验层，返回所需的扩展列表--1
    std::vector<const char*> getRequiredExtensions(){
        uint32_t glfwExtensionCount =0;
        const char** glfwExtensions;
        glfwExtensions=glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> extensions(glfwExtensions,
                                            glfwExtensions+glfwExtensionCount);
        if(enableValidationLayers){
            //需要使用 VK_EXT_debug_utils 扩展，设置回调函数来接受调试信息
            //如果启用校验层,添加调试报告相关的扩展
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        return extensions;
    }
    /**
      第一个参--指定了消息的级别，可以使用比较运算符来过滤处理一定级别以上的调试信息
    它可以是下面的值：
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT：诊断信息
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT：资源创建之类的信息
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT：警告信息
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT：不合法和可能造成崩溃的操作信息

    第二个参数--消息的类型，如下：
    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT：
    发生了一些与规范和性能无关的事件
    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT：
    出现了违反规范的情况或发生了一个可能的错误
    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT：
    进行了可能影响 Vulkan 性能的行为

    第三个参--一个指向 VkDebugUtilsMessengerCallbackDataEXT 结构体的指针
    包含了下面这些非常重要的成员：
    pMessage：一个以 null 结尾的包含调试信息的字符串
    pObjects：存储有和消息相关的 Vulkan 对象句柄的数组
    objectCount：数组中的对象个数

    最后一个参数 pUserData 是一个指向了我们设置回调函数时，传递的数据的指针

    回调函数返回了一个布尔值，用来表示引发校验层处理的 Vulkan API调用是否被中断。
    如果返回值为 true，对应 Vulkan API 调用就会返回
    VK_ERROR_VALIDATION_FAILED_EXT 错误代码。
    通常，只在测试校验层本身时会返回 true，其余情况下，回调函数应该返回 VK_FALSE
      */
    //接受调试信息的回调函数,以 vkDebugUtilsMessengerCallbackEXT 为原型
    //使用 VKAPI_ATTR 和 VKAPI_CALL 定义，确保它可以被 Vulkan 库调用--1
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData){
        std::cerr<<"validation layer: "<<pCallbackData->pMessage<<std::endl;
        return VK_FALSE;
    }
    //检测设备支持的队列族,查找出满足我们需求的队列族,这一函数会返回满足需求得队列族的索引--2
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device){
        uint32_t queueFamilyCount = 0;
        //获取设备的队列族个数
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
        VkBool32 presentSupport = false;
        for(const auto& queueFamily : queueFamilies){
            //VK_QUEUE_GRAPHICS_BIT表示支持图形指令
            if(queueFamily.queueCount>0 &&
                    queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT){
                indices.graphicsFamily = i;
            }
            //查找带有呈现图像到窗口表面能力的队列族
            vkGetPhysicalDeviceSurfaceSupportKHR(
                        device,i,surface,&presentSupport);
            if(queueFamily.queueCount>0 && presentSupport){
                indices.presentFamily = i;
            }
            /**
            说明：即使绘制指令队列族和呈现队列族是同一个队列族，
            我们也按照它们是不同的队列族来对待。
            显式地指定绘制和呈现队列族是同一个的物理设备来提高性能表现。
            */
            if(indices.isComplete()){
                break;
            }
            i++;
        }
        return indices;
    }
    /**
    只检查交换链是否可用还不够，交换链可能与我们的窗口表面不兼容。
    创建交换链所要进行的设置要比 Vulkan 实例和设备创建多得多，
    在进行交换链创建之前需要我们查询更多的信息.
    有三种最基本的属性，需要我们检查：
    1.基础表面特性 (交换链的最小/最大图像数量，最小/最大图像宽度、高度)
    2.表面格式 (像素格式，颜色空间)
    3.可用的呈现模式
    与交换链信息查询有关的函数都需要VkPhysicalDevice 对象和 VkSurfaceKHR作为参数,
    它们是交换链的核心组件
      */
    //查询交换链支持细节
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device){
        SwapChainSupportDetails details;
        //查询基础表面特性
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                    device,surface,&details.capabilities);
        //查询表面支持的格式,确保向量的空间足以容纳所有格式结构体
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(
                    device,surface,&formatCount,nullptr);
        if(formatCount != 0){
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(
                        device,surface,&formatCount,details.formats.data());
        }
        //查询支持的呈现模式
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(
                    device,surface,&presentModeCount,nullptr);
        if(presentModeCount != 0){
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(
                        device,surface,&presentModeCount,
                        details.presentModes.data());
        }

        return details;
    }
    //重建交换链
    void recreateSwapChain(){
        //设置应用程序在窗口最小化后停止渲染，直到窗口重新可见时重建交换链
        int width=0,height = 0;
        while(width == 0 || height == 0){
            glfwGetFramebufferSize(window,&width,&height);
            glfwWaitEvents();
        }
        //等待设备处于空闲状态，避免在对象的使用过程中将其清除重建
        vkDeviceWaitIdle(device);

        cleanupSwapChain();//清除交换链相关

        //重新创建了交换链
        createSwapChain();
        //图形视图是直接依赖于交换链图像的，所以也需要被重建
        createImageViews();
        //渲染流程依赖于交换链图像的格式,窗口大小改变不会引起使用的交换链图像格式改变
        createRenderPass();
        //视口和裁剪矩形在管线创建时被指定，窗口大小改变，这些设置也需要修改
        //我们可以通过使用动态状态来设置视口和裁剪矩形来避免重建管线
        createGraphicsPipeline();
        createDepthResources();
        //帧缓冲和指令缓冲直接依赖于交换链图像
        createFramebuffers();
        createCommandBuffers();
    }
    //清除交换链相关
    void cleanupSwapChain(){
        //销毁图像视图对象
        vkDestroyImageView(device, depthImageView, nullptr);
        //销毁深度图像
        vkDestroyImage(device, depthImage, nullptr);
        //释放深度图像内存
        vkFreeMemory(device, depthImageMemory, nullptr);
        //销毁帧缓冲对象
        for(auto framebuffer : swapChainFramebuffers){
            vkDestroyFramebuffer(device,framebuffer,nullptr);
        }
        //清除分配的指令缓冲对象
        vkFreeCommandBuffers(device,commandPool ,
           static_cast<uint32_t>(commandBuffers.size()),commandBuffers.data());
        //销毁管线对象
        vkDestroyPipeline ( device , graphicsPipeline , nullptr );
        //销毁管线布局对象
        vkDestroyPipelineLayout ( device , pipelineLayout , nullptr);
        //销毁渲染流程对象
        vkDestroyRenderPass ( device , renderPass , nullptr );
        //销毁图像视图
        for(auto imageView : swapChainImageViews){
            vkDestroyImageView(device,imageView,nullptr);
        }
        //销毁交换链对象，在逻辑设备被清除前调用
        vkDestroySwapchainKHR(device,swapChain,nullptr);
    }
    //创建顶点缓冲
    void createVertexBuffer(){
        VkDeviceSize bufferSize = sizeof(vertices[0])*vertices.size();
        //使用 CPU 可见的缓冲作为临时缓冲，使用显卡读取较快的缓冲作为真正的顶点缓冲
        VkBuffer stagingBuffer ;//缓冲对象存放 CPU 加载的顶点数据
        VkDeviceMemory stagingBufferMemory ;//缓冲对象内存
        /**
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT：缓冲可以被用作内存传输操作的数据来源。
        VK_BUFFER_USAGE_TRANSFER_DST_BIT：缓冲可以被用作内存传输操作的目的缓冲
         */
        createBuffer(bufferSize,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer,stagingBufferMemory);

        //将顶点数据复制到缓冲中
        void* data;
        /**
        vkMapMemory 函数允许我们通过给定的内存偏移值和内存大小访问特定的内存资源。
        这里我们使用的偏移值和内存大小分别时 0 和 bufferInfo.size。
        还有一个特殊值 VK_WHOLE_SIZE 可以用来映射整个申请的内存。
        vkMapMemory 函数的倒数第二个参数可以用来指定一个标记，暂不可用,必须将其设置为 0。
        最后一个参数用于返回内存映射后的地址。
          */
        //vkMapMemory将缓冲关联的内存映射到 CPU 可以访问的内存
        vkMapMemory(device,stagingBufferMemory,0,bufferSize,0,&data);
        /**
        驱动程序可能并不会立即复制数据到缓冲关联的内存中去，
        这是由于现代处理器都存在缓存这一设计，写入内存的数据并不一定在多个核心同时可见，
        有下面两种方法可以保证数据被立即复制到缓冲关联的内存中去:
        1.使用带有VK_MEMORY_PROPERTY_HOST_COHERENT_BIT属性的内存类型,
          保证内存可见的一致性
        2.写入数据到映射的内存后，调用 vkFlushMappedMemoryRanges 函数，
          读取映射的内存数据前调用 vkInvalidateMappedMemoryRanges函数

        第一种方法，它可以保证映射的内存的内容和缓冲关联的内存的内容一致。
        但使用这种方式，会比第二种方式些许降低性能表现
          */
        //将顶点数据复制到映射后的内存
        memcpy(data,vertices.data(),(size_t)bufferSize);
        //结束内存映射
        vkUnmapMemory(device,stagingBufferMemory);

        createBuffer(bufferSize,VK_BUFFER_USAGE_TRANSFER_DST_BIT|
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     vertexBuffer,vertexBufferMemory);
        /**
        vertexBuffer 现在关联的内存是设备所有的，不能 vkMapMemory 函数
        对它关联的内存进行映射。我们只能通过 stagingBuffer 来向 vertexBuffer复制数据。
        我们需要使用标记指明我们使用缓冲进行传输操作.
          */
        copyBuffer(stagingBuffer , vertexBuffer , bufferSize ) ;
        //清除我们使用的缓冲对象和它关联的内存对象
        vkDestroyBuffer(device , stagingBuffer , nullptr ) ;
        vkFreeMemory(device , stagingBufferMemory , nullptr ) ;
    }
    /**
     * @brief findMemoryType
     * @param typeFilter -- 指定我们需要的内存类型的位域
     * @param properties
     *  显卡可以分配不同类型的内存作为缓冲使用。不同类型的内存所允许进行的操作以及
        操作的效率有所不同。我们需要结合自己的需求选择最合适的内存类型使用
     */
    //选择最合适的内存类型使用
    uint32_t findMemoryType(uint32_t typeFilter,
                            VkMemoryPropertyFlags properties){
        /**
        vkGetPhysicalDeviceMemoryProperties 函数返回的
        VkPhysicalDeviceMemoryProperties结构体包含了memoryTypes和memoryHeaps变量。
        memoryHeaps 数组成员变量中的每个元素是一种内存来源，比如显存以及
                    显存用尽后的位于主内存种的交换空间
          */
        //查询物理设备可用的内存类型
        VkPhysicalDeviceMemoryProperties memProperties ;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice ,&memProperties);
        //遍历数组，查找缓冲可用的内存类型
        for(uint32_t i=0;i<memProperties.memoryTypeCount;i++){
            //检测它是否与我们需要的属性的位域完全相同
            if((typeFilter & (1 << i )) &&
               (memProperties.memoryTypes[i].propertyFlags & properties) ==
                    properties){
                return i;
            }
        }
        throw std::runtime_error("failed to find a suitable memory type!");
    }
    //创建缓冲--方便地使用不同的缓冲大小，内存类型来创建我们需要的缓冲
    //最后两个参数用于返回创建的缓冲对象和它关联的内存对象
    void createBuffer(VkDeviceSize size,VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties , VkBuffer& buffer,
                      VkDeviceMemory& bufferMemory){
        //同createVertexBuffer基本相同
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType =  VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        //指定要创建的缓冲所占字节大小
        bufferInfo.size = size;
        //指定缓冲中的数据的使用目的--这里存储顶点数据
        bufferInfo.usage = usage;
        //和交换链图像一样，缓冲可以被特定的队列族所拥有，也可以同时在多个队列族之前共享
        //这里使用独有模式
        bufferInfo.sharingMode =  VK_SHARING_MODE_EXCLUSIVE;
        //配置缓冲的内存稀疏程度
        bufferInfo.flags = 0;
        if(vkCreateBuffer(device,&bufferInfo,nullptr,
                          &buffer) != VK_SUCCESS){
            throw std::runtime_error("failed to create buffer!");
        }
        /**
        vkGetBufferMemoryRequirements 函数返回的 VkMemoryRequirements
        结构体有下面这三个成员变量：
        size：缓冲需要的内存的字节大小，它可能和 bufferInfo.size 的值不同
        alignment：缓冲在实际被分配的内存中的开始位置。
                它的值依赖于bufferInfo.usage 和 bufferInfo.flags。
        memoryTypeBIts：指示适合该缓冲使用的内存类型的位域
          */
        //获取缓冲的内存需求
        VkMemoryRequirements memRequirements ;
        vkGetBufferMemoryRequirements(device,buffer,&memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;//内存大小
        /**
        我们需要位域满足 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT(用于从 CPU 写入数据)
        和 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT的内存类型
          */
        allocInfo.memoryTypeIndex = findMemoryType(
                    memRequirements.memoryTypeBits,properties);
        //分配内存
        if(vkAllocateMemory(device,&allocInfo,nullptr,
                            &bufferMemory)!=VK_SUCCESS){
            throw std::runtime_error("failed to allocate buffer memory!");
        }
        /**
        第四个参数是偏移值。这里我们将内存用作顶点缓冲，可以将其设置为 0。
        偏移值需要满足能够被 memRequirements.alignment 整除
          */
        //将分配的内存和缓冲对象进行关联
        vkBindBufferMemory(device,buffer,bufferMemory,0);
    }
    //用于在缓冲之间复制数据
    void copyBuffer( VkBuffer srcBuffer , VkBuffer dstBuffer,
                      VkDeviceSize size){
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        //指定了复制操作的源缓冲位置偏移，目的缓冲位置偏移，以及要复制的数据长度
        VkBufferCopy copyRegion = {};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;//指定要复制的数据长度
        //进行缓冲的复制
        vkCmdCopyBuffer(commandBuffer,srcBuffer,dstBuffer,1,&copyRegion);

        endSingleTimeCommands(commandBuffer) ;
    }
    //创建索引缓冲--同创建顶点缓冲方式相同
    void createIndexBuffer(){
        VkDeviceSize bufferSize = sizeof(indices[0])*indices.size();
        VkBuffer stagingBuffer ;//缓冲对象存放 CPU 加载的数据
        VkDeviceMemory stagingBufferMemory ;//缓冲对象内存
        createBuffer(bufferSize,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer,stagingBufferMemory);

        //将顶点数据复制到缓冲中
        void* data;
        vkMapMemory(device,stagingBufferMemory,0,bufferSize,0,&data);
        memcpy(data,indices.data(),(size_t)bufferSize);
        //结束内存映射
        vkUnmapMemory(device,stagingBufferMemory);

        createBuffer(bufferSize,VK_BUFFER_USAGE_TRANSFER_DST_BIT|
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     indexBuffer,indexBufferMemory);

        copyBuffer(stagingBuffer , indexBuffer , bufferSize ) ;

        vkDestroyBuffer(device , stagingBuffer , nullptr ) ;
        vkFreeMemory(device , stagingBufferMemory , nullptr ) ;
    }
    //提供着色器使用的每一个描述符绑定信息
    void createDescriptorSetLayout(){
        //描述每一个绑定
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        //指定着色器使用的描述符绑定
        uboLayoutBinding.binding = 0;
        //描述符类型,这里指定的是一个 uniform 缓冲对象
        //着色器变量可以用来表示 uniform 缓冲对象数组
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        //数组中元素的个数
        uboLayoutBinding.descriptorCount = 1;
        //在哪一个着色器阶段被使用,这里只在顶点着色器使用
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        //用于和图像采样相关的描述符
        uboLayoutBinding.pImmutableSamplers = nullptr ;

        //填写用于组合图像采样器描述符的 VkDescriptorSetLayoutBinding 结构体信息
        VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
        samplerLayoutBinding.binding = 1;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.pImmutableSamplers = nullptr;
        /**
        stageFlags 成员变量指明在片段着色器中使用组合图像采样器描述符。
        在顶点着色器也可以进行纹理采样，一个常见的用途是在顶点着色
        器中使用高度图纹理来对顶点进行变形
          */
        samplerLayoutBinding.stageFlags =  VK_SHADER_STAGE_FRAGMENT_BIT;

        std::array<VkDescriptorSetLayoutBinding,2> bindings = {
            uboLayoutBinding,samplerLayoutBinding
        };

        //创建描述符布局相关信息
        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        if(vkCreateDescriptorSetLayout(device,&layoutInfo,nullptr,
                                       &descriptorSetLayout)!= VK_SUCCESS){
            throw std::runtime_error("failed to create descriptor set layout");
        }
    }
    //分配uniform 缓冲对象
    void createUniformBuffer(){
        VkDeviceSize buffersize = sizeof(UniformBufferObject);
        uniformBuffers.resize(swapChainImages.size());
        uniformBuffersMemory.resize(swapChainImages.size());
        for(size_t i=0;i<swapChainImages.size();i++){
            createBuffer(buffersize,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         uniformBuffers[i],uniformBuffersMemory[i]);
        }
    }
    //更新uniform 缓冲对象--可以在每一帧产生一个新的变换矩阵
    void updateUniformBuffer(uint32_t currentImage){
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now() ;
        float time = std::chrono::duration<float,std::chrono::seconds::period>(
                    currentTime - startTime).count();
        //我们在uniform缓冲对象中定义我们的MVP变换矩阵
        //模型的渲染被我们设计成绕 Z 轴渲染 time 弧度。
        UniformBufferObject ubo = {};
        /**
          glm::rotate 函数以矩阵，旋转角度和旋转轴作为参数
          glm::mat4(1.0f)用于构造单位矩阵
          通过 time * glm::radians(90.0f) 完成每秒旋转 90 度的操作。
          */
        ubo.model = glm::rotate(glm::mat4(1.0f),time*glm::radians(90.0f),
                                glm::vec3(0.0f,0.0f,1.0f));
        /**
         视图变换矩阵
        glm::lookAt 函数以观察者位置，视点坐标和向上向量为参数生成视图变换矩阵
         */
        ubo.view = glm::lookAt(glm::vec3(2.0f,2.0f,2.0f),
                        glm::vec3(0.0f,0.0f,0.0f),glm::vec3(0.0f,0.0f,1.0f));
        /**
          透视变换矩阵
        glm::perspective 函数以视域的垂直角度，
            视域的宽高比以及近平面和远平面距离为参数生成透视变换矩阵
        注意：窗口大小改变后应该使用当前交换链范围来重新计算宽高比
          */
        ubo.proj = glm::perspective(glm::radians(45.0f),
                        swapChainExtent.width/(float)swapChainExtent.height,
                                    0.1f,10.0f);
        /**
        GLM 库最初是为 OpenGL 设计的，它的裁剪坐标的 Y 轴和 Vulkan是相反的。
        我们可以通过将投影矩阵的 Y 轴缩放系数符号取反来使投影矩阵和 Vulkan 的要求一致。
        如果不这样做，渲染出来的图像会被倒置.
          */
        ubo.proj[1][1] *= -1;
        //将最后的变换矩阵数据复制到当前帧对应的 uniform 缓冲中
        void* data ;
        vkMapMemory( device , uniformBuffersMemory[currentImage],0,
                     sizeof(ubo) , 0 , &data );
        memcpy( data , &ubo , sizeof(ubo) );
        vkUnmapMemory( device , uniformBuffersMemory[currentImage]);
        /**
        对于在着色器中使用的需要频繁修改的数据，这样使用 UBO 并非最佳方式。
        还有一种更加高效的传递少量数据到着色器的方法,之后说
          */
    }
    /**
     * @brief createDescriptorPool
     *描述符集不能被直接创建，需要通过描述符池来分配
     */
    //描述符池的创建
    void createDescriptorPool(){
        //添加一个用于组合图像采样器描述符的VkDescriptorPoolSize 结构体信息
        std::array<VkDescriptorPoolSize,2> poolSizes = {};
        //指定描述符池可以分配的描述符集
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount =
                static_cast<uint32_t>(swapChainImages.size());

        //添加图像采样器描述符
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount =
                static_cast<uint32_t>(swapChainImages.size());

        //指定描述符池的大小，我们会在每一帧分配一个描述符
        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount =
                static_cast<uint32_t>(poolSizes.size());//最大独立描述符个数外
        poolInfo.pPoolSizes = poolSizes.data();
        //指定可以分配的最大描述符集个数
        poolInfo.maxSets =
                static_cast<uint32_t>(swapChainImages.size());;

        if(vkCreateDescriptorPool(device,&poolInfo,nullptr,
                                  &descriptorPool) != VK_SUCCESS){
            throw std::runtime_error("failed to create descriptor pool!");
        }
    }
    //创建描述符集对象
    void createDescriptorSets(){
        std::vector<VkDescriptorSetLayout> layouts(swapChainImages.size(),
                                                   descriptorSetLayout);
        //创建描述符集相关信息
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        //指定分配描述符集对象的描述符池
        allocInfo.descriptorPool = descriptorPool;
        //分配的描述符集数量
        allocInfo.descriptorSetCount =
                static_cast<uint32_t>(swapChainImages.size());
        //使用的描述符布局
        allocInfo.pSetLayouts = layouts.data();

        /**
        在这里，我们为每一个交换链图像使用相同的描述符布局创建对应的描述符集。
        但由于描述符布局对象个数要匹配描述符集对象个数，所以，
        我们还是需要使用多个相同的描述符布局对象。
          */
        descriptorSets.resize(swapChainImages.size());
        //分配地描述符集对象，每一个都带有一个 uniform 缓冲描述符
        if(vkAllocateDescriptorSets( device , &allocInfo,
                                     descriptorSets.data()) != VK_SUCCESS){
            throw std::runtime_error("failed to allocate descriptor sets!");
        }
        //配置描述符集对象
        for(size_t i =0;i<swapChainImages.size();i++){
            //配置描述符引用的缓冲对象
            VkDescriptorBufferInfo bufferInfo = {};
            //以指定缓冲对象
            bufferInfo.buffer = uniformBuffers[i];
            bufferInfo.offset = 0;
            //可以访问的数据范围,
            //需要使用整个缓冲，可以将range成员变量的值设置为 VK_WHOLE_SIZE
            bufferInfo.range = sizeof(UniformBufferObject);

            //绑定图像和采样器到描述符集中的描述符
            VkDescriptorImageInfo imageInfo = {};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView =  textureImageView;
            imageInfo.sampler = textureSampler;
            std::array<VkWriteDescriptorSet , 2> descriptorWrites = {};

            //更新描述符的配置
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            //指定要更新的描述符集对象
            descriptorWrites[0].dstSet = descriptorSets[i];
            /**
            在这里，我们将 uniform 缓冲绑定到索引 0。需要注意描述符可以是数组，
            所以我们还需要指定数组的第一个元素的索引，在这里，我们
            没有使用数组作为描述符，将索引指定为 0 即可
              */
            //缓冲绑定
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            //指定描述符的类型
            descriptorWrites[0].descriptorType =
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[0].descriptorCount = 1;//指定描述符的数量
            //指定描述符引用的缓冲数据
            descriptorWrites[0].pBufferInfo = &bufferInfo;
            //指定描述符引用的图像数据
            descriptorWrites[0].pImageInfo = nullptr;
            //指定描述符引用的缓冲视图
            descriptorWrites[0].pTexelBufferView = nullptr;

            //更新图像和采样器到描述符配置
            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = descriptorSets[i];
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType =
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[1].descriptorCount = 1;//指定描述符的数量
            descriptorWrites[1].pImageInfo = &imageInfo;
            /**
             * @brief vkUpdateDescriptorSets
             * 函数可以接受两个数组作为参数：
             * VkWriteDescriptorSet 结构体数组和 VkCopyDescriptorSet 结构体数组。
             * 后者被用来复制描述符对象
             */
            vkUpdateDescriptorSets(device,
                    static_cast<uint32_t>(descriptorWrites.size()),
                                   descriptorWrites.data(),0,nullptr);
            //更新描述符需要使用图像资源信息。至此，我们就可以在着色器中使用描述符了
        }
    }
    void copyBufferToImage(VkBuffer buffer , VkImage image ,
                             uint32_t width , uint32_t height ){
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        //指定将数据复制到图像的哪一部分
        VkBufferImageCopy region = {};
        //指定要复制的数据在缓冲中的偏移位置
        region.bufferOffset = 0;
        /**
        bufferRowLength 和 bufferImageHeight 成员变量用于指定数据在内存中的存放方式.
        通过这两个成员变量我们可以对每行图像数据使用额外的空间进行对齐。
        将这两个成员变量的值都设置为 0，数据将会在内存中被紧凑存放
          */
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        //imageSubresource、imageOffset 和 imageExtent 成员变量用于指定
        //数据被复制到图像的哪一部分
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;

        region.imageOffset = {0,0,0};
        region.imageExtent = {width,height,1};
        /**
        vkCmdCopyBufferToImage 函数的第 4 个参数用于指定目的图像当前使用的图像布局。
        这里我们假设图像已经被变换为最适合作为复制目的的布局。
        我们只复制了一张图像，实际上是可以指定一个 VkBufferImageCopy 数组来一次从
        一个缓冲复制数据到多个不同的图像对象
          */
        //从缓冲复制数据到图像
        vkCmdCopyBufferToImage(commandBuffer,buffer,image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);

        endSingleTimeCommands( commandBuffer );
    }

    //加载图像数据到一个Vulkan 图像对象,用指令缓冲来完成加载
    void createTextureImage(){
        int texWidth , texHeight , texChannels ;
        /**
        以图像文件路径和需要加载的颜色通道作为参数来加载图像文件
        使用 STBI_rgb_alpha 通道参数可以强制载入 alpha 通道，
        即使图像数据不包含这一通道，也会被添加上一个默认的alpha值作为alpha
        通道的图像数据，这为我们的处理带来了方便。
        stbi_load 函数还可以返回图像的宽度，高度和图像数据实际存在的颜色通
        道。stbi_load 函数的返回值是一个指向解析后的图像像素数据的指针。使
        用 STBI_rgba_alpha 作为通道参数，每个像素需要 4 个字节存储，所有
        像素按照行的方式依次存储.
          */
        stbi_uc* pixels = stbi_load(TEXTURE_PATH.c_str(),
                                   &texWidth,&texHeight,&texChannels,
                                   STBI_rgb_alpha);
        //图像需要的总字节数
        VkDeviceSize imageSize = texWidth * texHeight * 4;
        if(!pixels){
            throw std::runtime_error("failed to load texture image!");
        }
        //计算细化级别个数
        mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(
                                          texWidth,texHeight))))+1;

        //同创建顶点缓冲步骤相同
        //使用 CPU 可见的缓冲作为临时缓冲,才能映射内存
        VkBuffer stagingBuffer ;
        VkDeviceMemory stagingBufferMemory ;
        createBuffer(imageSize,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer,stagingBufferMemory);

        //映射内存，将图像数据复制到缓冲中：
        void* data;
        //vkMapMemory将缓冲关联的内存映射到 CPU 可以访问的内存
        vkMapMemory(device,stagingBufferMemory,0,imageSize,0,&data);
        //将顶点数据复制到映射后的内存
        memcpy(data,pixels,static_cast<size_t>(imageSize));
        //结束内存映射
        vkUnmapMemory(device,stagingBufferMemory);

        //清除图像像素数据
        stbi_image_free( pixels) ;

        /*createImage(texWidth,texHeight,mipLevels,VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    textureImage,textureImageMemory);--old*/
        createImage(texWidth,texHeight,mipLevels,VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT|
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    textureImage,textureImageMemory);
        /**
        复制暂存缓冲中的数据到纹理图像，我们需要进行下面两步操作：
        1. 变换纹理图像到 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        2. 执行图像数据复制操作

        这里我们创建的图像对象使用 VK_IMAGE_LAYOUT_UNDEFINED布局，
        所以转换图像布局时应该将 VK_IMAGE_LAYOUT_UNDEFINED指定为旧布局。
        需要注意的是我们之所以这样设置是因为我们不需要读取复制操作之前的图像内容。
          */
        transitionImageLayout(textureImage,VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              mipLevels);

        copyBufferToImage(stagingBuffer, textureImage,
                          static_cast<uint32_t>(texWidth),
                          static_cast<uint32_t>(texHeight));

        //为了能够在着色器中采样纹理图像数据,我们还需要进行一次图像布局变换
        /*transitionImageLayout(textureImage,VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              mipLevels);*/
        /**
          注意：如果在开启校验层的情况下运行程序，会发现校验层报告
        transitionImageLayout 中使用的访问掩码和管线阶段是无效的。
        我们需要根据布局变换设置 transitionImageLayout。我们需要处理两种变换：
        1. 未定义 -> 传输目的：传输操作的数据写入不需要等待
        2. 传输目的 -> 着色器读取：着色器读取图像数据需要等待传输操作的写入结束。
          */

        //清除我们使用的缓冲对象和它关联的内存对象
        vkDestroyBuffer(device , stagingBuffer , nullptr ) ;
        vkFreeMemory(device , stagingBufferMemory , nullptr ) ;
        generateMipmaps(textureImage, VK_FORMAT_R8G8B8A8_UNORM,
                        texWidth, texHeight, mipLevels);
    }
    //创建图像
    void createImage(uint32_t width , uint32_t height ,uint32_t mipLevels,
                     VkFormat format ,VkImageTiling tiling ,
                     VkImageUsageFlags usage,VkMemoryPropertyFlags properties,
                     VkImage& image ,VkDeviceMemory& imageMemory){

        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        /**
        图像类型可以是一维，二维和三维图像。一维图像通常被用来存储数组数据或梯度数据。
        二维图像通常被用来存储纹理。三维图像通常被用类存储体素数据。
          */
        //指定图像类型,确定图像数据的坐标系
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        //extent指定图像在每个维度的范围，也就是在每个坐标轴有多少纹素
        imageInfo.extent.width =  width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;//二维为1
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 1;
        //图像数据格式,这里使用的是图像库解析的像素数据格式
        imageInfo.format = format;
        /**
          tiling 成员变量可以是下面这两个值之一：
          VK_IMAGE_TILING_LINEAR：纹素以行主序的方式排列
          VK_IMAGE_TILING_OPTIMAL：纹素以一种对访问优化的方式排列

        tiling 成员变量的设置在之后不可以修改。如果读者需要直接访问图
        像数据，应该将 tiling 成员变量设置为 VK_IMAGE_TILING_LINEAR。

        由于这里我们使用暂存缓冲而不是暂存图像来存储图像数据
        所以设为后者来获得更好的访问性能.
          */
        imageInfo.tiling = tiling;
        /**
          initialLayout 成员变量可以设置为下面这些值:
        VK_IMAGE_LAYOUT_UNDEFINED：GPU 不可用，纹素在第一次变换会被丢弃
        VK_IMAGE_LAYOUT_PREINITIALIZED：GPU 不可用，纹素在第一次变换会被保留

        大多数情况下对于第一次变换，纹素没有保留的必要。但如果使
        用图像对象以及 VK_IMAGE_TILING_LINEAR 标记来暂存纹理数据，
        这种情况下，纹理数据作为数据传输来源不会被丢弃。但在这里，我们
        是将图像对象作为传输数据的接收方，将纹理数据从缓冲对象传输到图
        像对象，所以我们不需要保留图像对象第一次变换时的纹理数据，使用
        VK_IMAGE_LAYOUT_UNDEFINED 更好。
          */
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        /**
        usage 成员变量的用法和创建缓冲时使用的 usage 成员变量用法相同。
        这里，我们创建的图像对象被用作传输数据的接收方。并且图像数据需要
        被着色器采样，所以我们使用下面两个标记
          */
        imageInfo.usage = usage;
        /**
        我们的图像对象只被一个队列族使用：支持传输操作的队列族。
        所以这里我们使用独占模式
          */
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        /**
        samples 成员变量用于设置多重采样。这一设置只对用作附着的图像对
        象有效，我们的图像不用于附着，将其设置为采样 1 次。有许多用于稀疏
        图像的优化标记可以使用。稀疏图像是一种离散存储图像数据的方法。比
        如，我们可以使用稀疏图像来存储体素地形，避免为“空气”部分分配内存.
          */
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        //没有使用 flags 标记,所以设为0
        imageInfo.flags = 0;
        //创建图像对象
        if(vkCreateImage(device,&imageInfo,nullptr,
                         &image)!=VK_SUCCESS){
            throw std::runtime_error("failed to create image!");
        }
        //获取缓冲的内存需求
        VkMemoryRequirements memRequirements ;
        vkGetImageMemoryRequirements(device,image,&memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;//内存大小
        allocInfo.memoryTypeIndex = findMemoryType(
                    memRequirements.memoryTypeBits,properties);
        //分配内存
        if(vkAllocateMemory(device,&allocInfo,nullptr,
                            &imageMemory)!=VK_SUCCESS){
            throw std::runtime_error("failed to allocate image memory!");
        }
        //将分配的内存和缓冲对象进行关联
        vkBindImageMemory(device,image,imageMemory,0);
    }
    //开始记录传输指令到指令缓冲
    VkCommandBuffer beginSingleTimeCommands(){
        /**
        我们需要一个支持内存传输指令的指令缓冲来记录内存传输指令，
        然后提交到内存传输指令队列执行内存传输。
        通常，我们会为内存传输指令使用的指令缓冲创建另外的指令池对象，
        这是因为内存传输指令的指令缓存通常生命周期很短，为它们使用独立的指令池对象，
        可以进行更好的优化。
        我们可以在创建指令池对象时为它指定VK_COMMAND_POOL_CREATE_TRANSIENT_BIT标记.
          */
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType =
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        //分配指令缓冲对象
        VkCommandBuffer commandBuffer;
        if(vkAllocateCommandBuffers(device,&allocInfo,
                                    &commandBuffer)!= VK_SUCCESS){
            throw std::runtime_error("failed to allocate command buffers!");
        }
        //开始记录内存传输指令
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType =
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        //flags告诉驱动程序我们如何使用这个指令缓冲，来让驱动程序进行更好的优化
        beginInfo.flags =
                VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if(vkBeginCommandBuffer(commandBuffer,&beginInfo)!=VK_SUCCESS){
            throw std::runtime_error(
                        "failed to begin recording command buffer.");
        }
        return commandBuffer;
    }
    //结束记录传输指令到指令缓冲
    void endSingleTimeCommands(VkCommandBuffer commandBuffer){
        //结束指令缓冲的记录操作，提交指令缓冲完成传输操作的执行
        vkEndCommandBuffer( commandBuffer ) ;
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit( graphicsQueue , 1 , &submitInfo , VK_NULL_HANDLE) ;
        /**
         有两种等待内存传输操作完成的方法：
         1.一种是使用栅栏 (fence)，通过 vkWaitForFences函数等待。
         2.通过 vkQueueWaitIdle 函数等待。
         使用栅栏 (fence) 可以同步多个不同的内存传输操作，给驱动程序的优化空间也更大
          */
        vkQueueWaitIdle( graphicsQueue ) ;//等待传输操作完成
        //清除我们使用的指令缓冲对象
        vkFreeCommandBuffers( device ,commandPool ,1 ,&commandBuffer);
    }
    /**
    如果我们使用的是缓冲对象而不是图像对象，那么就可以记录传输指
    令，然后调用 vkCmdCopyBufferToImage 函数结束工作，但这一指令需要
    图像满足一定的布局要求，所以需要我们编写一个新的函数来进行图像布局变换
      */
    //变换图像布局
    void transitionImageLayout(VkImage image, VkFormat format,
                            VkImageLayout oldLayout,VkImageLayout newLayout,
                               uint32_t mipLevels){

        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        //对于缓冲对象也有一个可以实现同样效果的缓冲内存屏障 (buffer memory barrier)
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        /**
          指定布局变换
        如果不需要访问之前的图像数据，可以将 oldLayout 设置为
        VK_IMAGE_LAYOUT_UNDEFINED来获得更好的性能表现
          */
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        /**
        如果读者使用屏障来传递队列族所有权，那么就需要对 srcQueueFamilyIndex
        和 dstQueueFamilyIndex 成员变量进行设置。如果读者不进行队列
        所有权传递，则必须将这两个成员变量的值设置为 VK_QUEUE_FAMILY_IGNORED
          */
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;//指定进行布局变换的图像对象
        //subresourceRange表示受影响的图像范围
        if(newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL){
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if(hasStencilComponent(format)){
                barrier.subresourceRange.aspectMask |=
                        VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        }else{
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }
        barrier.subresourceRange.baseMipLevel = 0;//不使用细分
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;//不使用细分
        /**
        我们需要指定在屏障之前必须发生的资源操作类型，以及必须等待屏
        障的资源操作类型。虽然我们已经使用 vkQueueWaitIdle 函数来手动地进
        行同步，但还是需要我们进行这一设置。但这一设置依赖旧布局和新布局，
        所以我们会在确定使用的布局变换后再来设置它
          */
        //barrier.srcAccessMask = 0;
        //barrier.dstAccessMask = 0;

        /**
        传输的写入操作必须在管线传输阶段进行。这里因为我们的写入
        操作不需要等待任何对象，我们可以指定一个空的的访问掩码，使用
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT 指定最早出现的管线阶
        段。需要注意 VK_PIPELINE_STAGE_TRANSFER_BIT 并非图形和计
        算管线中真实存在的管线阶段，它实际上是一个伪阶段，出现在传输操作发生时.

        图像数据需要在片段着色器读取，所以我们指定了片段着色器管线阶段的读取访问掩码。

        指令缓冲的提交会隐式地进行 VK_ACCESS_HOST_WRITE_BIT
        同步。因为 transitionImageLayout 函数执行的指令缓冲只包含了一条指
        令，如果布局转换依赖 VK_ACCESS_HOST_WRITE_BIT，我们可以
        通过设置 srcAccessMask 的值为 0 来使用这一隐含的同步。不过，我们最
        好显式地定义一切，依赖于隐含属性，不就变得和 OpenGL 一样容易出现错误.

        有一个特殊的支持所有操作的图像布局类型：VK_IMAGE_LAYOUT_GENERAL。
        但它并不保证能为所有操作都带来最佳性能表现。对于一些特殊情况，比
        如将图像同时用作输入和输出对象，或读取一个已经改变初始化时的布局的图像时，需要使用它。

        目前为止，我们编写的包含提交指令操作的辅助函数都被设置为通过
        等待队列空闲来进行同步。对于实用的程序，更推荐组合多个操作到一个
        指令缓冲对象，通过异步执行来增大吞吐量，特别对于 createTextureImage
        函数中的变换和数据复制操作，这样做可以获得很大的性能提升。我们可
        以编写一个叫做 setupCommandBuffer 的辅助函数来记录指令，编写一个
        叫做 flushSetupCommands 来提交执行记录的指令，通过实验得到最佳的同步策略
          */
        //根据布局变换设置
        VkPipelineStageFlags sourceStage ;
        VkPipelineStageFlags destinationStage ;
        if(oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL){
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }else if(oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                 newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL){
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }else if(oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                 newLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL){
            //尽管这里我们没有使用模板颜色通道，我们还是要对它进行变换处理
            /**
            深度缓冲数据会在进行深度测试时被读取，用来检测片段是否可以覆盖之前的片段。
            这一读取过程发生在 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT，
            如果片段可以覆盖之前的片段，新的深度缓冲数据会在
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT写入。
            我们应该使用与要进行的操作相匹配的管线阶段进行同步操作.
              */
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask =
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }else{
            throw std::invalid_argument("unsupported layout transition!");
        }

        /**
          vkCmdPipelineBarrier参数：
          1.指定发生在屏障之前的管线阶段
          2.于指定发生在屏障之后的管线阶段，如果读者想要在一个屏障之后读取 uniform，
          应该指定 VK_ACCESS_UNIFORM_READ_BIT使用标记和最早读取uniform的着色器阶段，
          比如 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT。
          当指定和使用标记不匹配的管线阶段时校验层会发出警告信息
          3.设置为 0 或 VK_DEPENDENCY_BY_REGION_BIT。
            设置为 VK_DEPENDENCY_BY_REGION_BIT 的话，屏障就变成了一个区域条件。
            这允许我们读取资源目前已经写入的那部分
          4-end:用于引用三种可用的管线屏障数组：内存屏障 (memory barriers)，
        缓冲内存屏障 (buffer memory barriers) 和图像内存屏障 (imagememory barriers)。
        我们这里使用的是图像内存屏障 (image memory barriers)。
        需要注意这里我们没有使用 VkFormat 参数，但之后用它进行特殊的变换操作
          */
        //提交管线屏障对象
        vkCmdPipelineBarrier(commandBuffer,sourceStage,destinationStage,
                             0,0,nullptr,0,nullptr,1,&barrier);

        endSingleTimeCommands( commandBuffer );
    }
    //创建纹理图像的图像视图对象
    void createTextureImageView(){
        textureImageView = createImageView(textureImage,
                                           VK_FORMAT_R8G8B8A8_UNORM,
                                           VK_IMAGE_ASPECT_COLOR_BIT,mipLevels);
    }
    //创建图像视图对象
    VkImageView createImageView(VkImage image , VkFormat format,
                           VkImageAspectFlags aspectFlags,uint32_t mipLevels){
        //设置图像视图结构体相关信息
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        //viewType 和 format 成员变量用于指定图像数据的解释方式。
        //viewType用于指定图像被看作是一维纹理、二维纹理、三维纹理还是立方体贴图
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        /**
        components 成员变量用于进行图像颜色通道的映射。
        比如，对于单色纹理，我们可以将所有颜色通道映射到红色通道。
        我们也可以直接将颜色通道的值映射为常数 0 或 1。在这里,我们只使用默认的映射

        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;*/
        //subresourceRange用于指定图像的用途和图像的哪一部分可以被访问
        //在这里，我们的图像被用作渲染目标，并且没有细分级别，只存在一个图层
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        VkImageView imageView;
        /**
        如果读者在编写 VR 一类的应用程序，可能会使用支持多个层次的交换链。
        这时，读者应该为每个图像创建多个图像视图，分别用来访问左眼和右眼两个不同的图层
          */
        //创建图像视图
        if(vkCreateImageView(device,&viewInfo,nullptr,
                             &imageView) != VK_SUCCESS){
            throw std::runtime_error("failed to create texture image view!");
        }
        return imageView;
        //有了图像视图，就可以将图像作为纹理使用，但作为渲染目标，还需要帧缓冲对象
    }
    //创建采样器对象
    void createTextureSampler(){
        //我们会在着色器中使用创建的采样器对象采样纹理数据
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        /**
        magFilter 和 minFilter 成员变量用于指定纹理需要放大和缩小时使用
        的插值方法。纹理放大会出现采样过密的问题，纹理缩小会出现采样过疏的问题
          */
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        /**
        addressModeU、addressModeV 和 addressModeW 用于指定寻址模式。
        这里的 U、V、W 对应 X、Y 和 Z 轴。它们的值可以是下面这些：
        • VK_SAMPLER_ADDRESS_MODE_REPEAT：
            采样超出图像范围时重复纹理
        • VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT：
            采样超出图像范围时重复镜像后的纹理
        • VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE：
            采样超出图像范围时使用距离最近的边界纹素
        • VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE：
            采样超出图像范围时使用镜像后距离最近的边界纹素
        • VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER：
            采样超出图像返回时返回设置的边界颜色

        在这里，我们的采样不会超出图像范围.

          */
        //VK_SAMPLER_ADDRESS_MODE_REPEAT 来实现平铺纹理的效果
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        /**
        anisotropyEnable 和 maxAnisotropy 成员变量和各向异性过滤相关。
        通常来说，只要性能允许，我们都会开启各向异性过滤。maxAnisotropy
        成员变量用于限定计算最终颜色使用的样本个数。maxAnisotropy 成员变
        量的值越小，采样的性能表现越好，但采样结果质量较低。目前为止，还
        没有图形硬件能够使用超过 16 个样本，同时即使可以使用超过 16 个样本，
        采样效果的增强也开始变得微乎其微。
          */
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16;
        /**
        borderColor 成员变量用于指定使用 VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
        寻址模式时采样超出图像范围时返回的边界颜色。边界颜色并非可以设置为任意颜色。
        它可以被设置为浮点或整型格式的黑色、白色或透明色
          */
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        /**
        unnormalizedCoordinates 成员变量用于指定采样使用的坐标系统。将
        其设置为 VK_TRUE 时，采样使用的坐标范围为 [0, texWidth) 和 [0,
        texHeight)。将其设置为 VK_FALSE，采样使用的坐标范围在所有轴都是
        [0, 1)。通常使用 VK_FALSE 的情况更常见，这种情况下我们可以使用相
        同的纹理坐标采样不同分辨率的纹理
          */
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        /**
        通过 compareEnable 和 compareOp 成员变量，我们可以将样本和一
        个设定的值进行比较，然后将比较结果用于之后的过滤操作。通常我们在
        进行阴影贴图时会使用它
          */
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        /**
        mipmapMode、mipLodBias、minLod 和 maxLod 成员变量用于设置
        分级细化 (mipmap)，它可以看作是过滤操作的一种
          */
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = static_cast<float>(mipLevels/2);
        samplerInfo.maxLod = static_cast<float>(mipLevels);
        /**
        需要注意，采样器对象并不引用特定的 VkImage 对象，它是一个用于
        访问纹理数据的接口。我们可以使用它来访问任意不同的图像，不管图像
        是一维的、二维的、还是三维的。这和一些旧的图形 API 将纹理图像和过
        滤设置绑定在一起进行采样是不同的
          */
        if(vkCreateSampler(device,&samplerInfo,nullptr,
                           &textureSampler) != VK_SUCCESS){
            throw std::runtime_error("failed to create texture sampler!");
        }
    }
    //配置深度图像需要的资源
    void createDepthResources(){
        //找一个可用的深度图像数据格式
        VkFormat depthFormat = findDepthFormat();
        /**
         * 我们已经具有足够的信息来创建深度图像对象，
         * 可以开始调用createImage 和 createImageView 函数来创建图像资源：
         */
        createImage(swapChainExtent.width, swapChainExtent.height ,1,
                    depthFormat , VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage ,
                    depthImageMemory);
        depthImageView = createImageView(depthImage , depthFormat,
                                         VK_IMAGE_ASPECT_DEPTH_BIT,1) ;
        /**
        接着，就是创建深度图像。通常，我们在渲染流程开始后首先会清除
        深度附着中的数据，不需要复制数据到深度图像中。我们需要对图像布局
        进行变换，来让它适合作为深度附着使用。由于这一图像变换只需要进行
        一次，这里我们使用管线障碍来同步图像变换：

        因为不需要深度图像之前的数据，所以我们使用VK_IMAGE_LAYOUT_UNDEFINED
          */
        transitionImageLayout(depthImage , depthFormat ,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,1);
    }
    /**
    可用的深度图像数据格式还依赖于tiling模式和使用标记，所以我们需要将这些信息作为函数参数，
    通过调用 vkGetPhysicalDeviceFormatProperties 函数查询可用的深度图像数据格式：
      */
    //查找一个既符合我们需求又被设备支持的图像数据格式
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates,
                                 VkImageTiling tiling,
                                 VkFormatFeatureFlags features){
        for(VkFormat format : candidates){
            /**
            VkFormatProperties 结构体包含了下面这些成员变量：
            • linearTilingFeatures：数据格式支持线性 tiling 模式
            • optimalTilingFeatures：数据格式支持优化 tiling 模式
            • bufferFeatures：数据格式支持缓冲
            目前我们只用到上面前两个成员变量，通过它们对应的 tiling 模式检
            测数据格式是否被支持：
             */
            VkFormatProperties props ;
            vkGetPhysicalDeviceFormatProperties(physicalDevice,
                                                format,&props);
            if(tiling == VK_IMAGE_TILING_LINEAR &&
                    (props.linearTilingFeatures & features) == features){
                return format;
            }else if(tiling == VK_IMAGE_TILING_OPTIMAL &&
                     (props.optimalTilingFeatures & features) == features){
                return format;
            }
        }
        throw std::runtime_error("failed to find supported format!");
    }
    //查找适合作为深度附着的图像数据格式
    VkFormat findDepthFormat(){
        /**
        需要注意这里使用的是 VK_FORMAT_FEATURE_ 类的标记而不是
        VK_IMAGE_USAGE_ 类标记，这一类标记的格式都包含了深度颜色通
        道，有一些还包含了模板颜色通道，但在这里，我们没有用到模板颜色通道。
        但如果使用的格式包含模板颜色通道，在进行图像布局变换时就需要考虑这一点。
          */
        return findSupportedFormat({VK_FORMAT_D32_SFLOAT,
                                    VK_FORMAT_D32_SFLOAT_S8_UINT,
                                   VK_FORMAT_D24_UNORM_S8_UINT},
                                   VK_IMAGE_TILING_OPTIMAL,
                               VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }
    //检测格式是否包含模板颜色通道
    bool hasStencilComponent(VkFormat format ){
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                format == VK_FORMAT_D24_UNORM_S8_UINT;
    }
    //载入模型文件,填充模型数据到 vertices 和 indices
    void loadModel(){
        /**
        一个 OBJ 模型文件包含了模型的位置、法线、纹理坐标和表面数据。
        表面数据包含了构成表面的多个顶点数据的索引。
        attrib 变量来存储载入的位置、法线和纹理坐标数据。
        shapes 变量存储独立的对象和它们的表面数据。
        每个表面数据包含了一个顶点数组，顶点数组中的每个顶点数据包含了顶点的
        位置索引、法线索引和纹理坐标索引。OBJ 模型文件格式允许为模型的每
        个表面定义材质和纹理数据，但在这里，我们没有用到。
        err 变量来存储载入模型文件时产生的错误和警告信息，
        比如载入时没有找到引用的材质信息。
        OBJ 模型文件中的表面数据可以包含任意数量的顶点数据，但我们的程序只能渲染三角形表面，
        这就需要进行转换将 OBJ 模型文件中的表面数据都转换为三角形表面。
        tinyobj::LoadObj 函数有一个可选的默认参数，可以设置在加载 OBJ 模型
        数据时将表面数据转换为三角形表面。由于这一设置是默认的，所以，我们不需要自己设置它。
         */
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn;
        std::string err;
        if(!tinyobj::LoadObj(&attrib,&shapes,&materials,&err,&warn,
                             MODEL_PATH.c_str())){
            throw std::runtime_error(warn+err);
        }
        /**
        我们需要达到索引缓冲节约空间的目的。三角形表面的顶点是被多个三角形表面共用的，
        而我们则是每个顶点都重新定义一次，vertices 向量包含了大量重复的顶点数据。
        我们可以将完全相同的顶点数据只保留一个，来解决空间。
        这一去重过程可以通过 STL 的 map 或unordered_map 来实现：
          */
        std::unordered_map<Vertex, uint32_t> uniqueVertices;

        //将加载的表面数据复制到我们的 vertices 和 indices 向量中
        for(const auto& shape : shapes){
            //载入的表面数据已经被三角形化，所以直接将它们复制到vertices 向量中
            for(const auto& index: shape.mesh.indices){
                Vertex vertex = {};
                /**
                为了简化 indices 数组的处理，我们这里假定每个顶点都是独一无
                二的，可以直接使用 indices 数组的当前大小作为顶点索引数据。上面
                代码中的 index 变量的类型为 tinyobj::index_t，这一类型的变量包含了
                vertex_index、normal_index 和 texcoord_index 三个成员变量。
                我们使用这三个成员变量来检索存储在 attrib 数组变量中的顶点数据：

                attrib.vertices 是一个浮点数组，并非 glm::vec3 数组，我们需要在使
                用索引检索顶点数据时首先要把索引值乘以 3 才能得到正确的顶点数据位
                置。对于纹理坐标数据，则乘以 2 进行检索。对于顶点位置数据，偏移值
                0 对应 X 坐标，偏移值 1 对应 Y 坐标，偏移值 2 对应 Z 坐标。对于纹理
                坐标数据，偏移值 0 对应 U 坐标，偏移值 1 对应 V 坐标
                  */
                vertex.pos = {
                    attrib.vertices[3*index.vertex_index+0],
                    attrib.vertices[3*index.vertex_index+1],
                    attrib.vertices[3*index.vertex_index+2]
                };
                /**
                Vulkan的纹理坐标的原点是左上角，而OBJ模型文件格式假设纹理坐标原点是左下角。
                所以需要反转纹理的 Y 坐标使两个对应
                  */
                vertex.texCoord = {
                    attrib.texcoords[2*index.texcoord_index+0],
                    1.0f - attrib.texcoords[2*index.texcoord_index+1]
                };
                vertex.color = {1.0f,1.0f,1.0f};
                //优化顶点结构
                if (uniqueVertices.count(vertex) == 0) {
                    uniqueVertices[vertex] =
                            static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                }

                indices.push_back(uniqueVertices[vertex]);
            }
        }
    }
    //生成原始纹理图像不同细化级别
    void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth,
                         int32_t texHeight, uint32_t mipLevels){
        VkFormatProperties formatProperties;
        //查询指定的纹理图像格式的属性
        vkGetPhysicalDeviceFormatProperties(physicalDevice,
                                            imageFormat, &formatProperties);
        /**
        VkFormatProperties 结构体包含了 linearTilingFeatures、
        optimalTilingFeatures 和 bufferFeatures 三个成员变量，
        每个成员变量描述了在使用对应模式下，可以使用的特性。
        我们的纹理图像使用优化的 tiling 模式创建,所以我们需要检查
        optimalTilingFeatures 成员变量来确定我们使用的
        纹理图像格式是否支持线性过滤特性：
          */
        if (!(formatProperties.optimalTilingFeatures &
              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
            throw std::runtime_error(
                    "texture image format does not support linear blitting!");
        }
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        //对多次图像布局变换进行同步
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.levelCount = 1;
        int32_t mipWidth = texWidth ;
        int32_t mipHeight = texHeight ;
        //来遍历所有细化级别,记录每个细化级别使用的VkCmdBlitImage 指令到指令缓冲中
        for(uint32_t i=1;i<mipLevels;i++){
            /**
            我们设置将细化级别为 i-1 的纹理图像变换到
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL布局。
            这一变换会在细化级别为 i-1 的纹理图像数据被写入后 (上一次的传
            输指令写入或 vkCmdCopyBufferToImage 指令写入) 进行。
            当前的传输指令会等待这一变换结束才会执行
              */
            barrier.subresourceRange.baseMipLevel = i-1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask =  VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(commandBuffer,VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,
                                 0,nullptr,1,&barrier);
            /**
            我们指定传输操作使用的纹理图像范围。
            这里将srcSubresource.mipLevel成员变量设置为i-1,也就是上一细化级别的纹理图像。
            将dstSubresource.mipLevel成员变量设置为i,也就是我们要生成的纹理图像的细化级别。srcOffsets 数组用于指定要传输的数据所在的三维图像区
            dstOffsets数组用于指定接收数据的三维图像区域。
            dstOffsets[1]的X分量和Y分量的值需要设置为上一细化级别纹理图像的一半。
            由于这里我们使用的是二维图像，二维图像的深度值都为 1，
            所以 srcOffsets[1] 和dstOffsets[1] 的 Z 分量都必须设置为 1。
              */
            VkImageBlit blit = {};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1,
                                   mipHeight > 1 ? mipHeight / 2 : 1, 1 };
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;
            //开始记录传输指令到指令缓冲
            vkCmdBlitImage(commandBuffer,
                image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit,
                VK_FILTER_LINEAR);
            /**
            设置屏障将细化级别为 i-1 的图像布局变换到
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL这一变换会等待当前的
            传输指令结束会才会进行。所有采样操作需要等待这一变换结束才能进行
              */
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                0, nullptr,
                0, nullptr,
                1, &barrier);
            //计算出下一次循环要使用的细化级别的图像大小
            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }
        /**
        插入一个管线障碍用于将最后一个细化级别的图像布局从
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL变换为
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL。
        这样做是因为最后一个细化级别的图像不会被作为传输指令的数据来源，
        所以就不会将布局变换为 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL，
        需要我们手动进行变换
          */
        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        endSingleTimeCommands( commandBuffer );
    }
};

int main(int argc, char *argv[])
{
    HelloTriangle hello;
    try{
        hello.run();
    }catch(const std::exception& e){
        //捕获并打印hello中抛出的异常
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
