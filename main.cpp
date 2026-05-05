#include <GL/glew.h>
#include <SFML/Window.hpp>
#include <SFML/OpenGL.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Vertex Shader
const char* vertexShaderSource = R"glsl(
#version 410 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTex;
out vec2 TexCoord;
void main() {
    gl_Position = vec4(aPos, 1.0);
    TexCoord = aTex;
}
)glsl";

// Tessellation Control Shader
const char* tcsSource = R"glsl(
#version 410 core
layout (vertices = 4) out;
in vec2 TexCoord[];
out vec2 TextureCoord[];
uniform vec3 cameraPos;
void main() {
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    TextureCoord[gl_InvocationID] = TexCoord[gl_InvocationID];
    if(gl_InvocationID == 0) {
        vec3 p0 = gl_in[0].gl_Position.xyz;
        vec3 p1 = gl_in[1].gl_Position.xyz;
        vec3 p2 = gl_in[2].gl_Position.xyz;
        vec3 p3 = gl_in[3].gl_Position.xyz;
        vec3 center = (p0 + p1 + p2 + p3) * 0.25;
        float dist = length(center - cameraPos);
        float maxLevel = 64.0;
        float tessLevel = clamp(maxLevel / (dist * 0.15 + 1.0), 1.0, maxLevel);
        gl_TessLevelOuter[0] = tessLevel;
        gl_TessLevelOuter[1] = tessLevel;
        gl_TessLevelOuter[2] = tessLevel;
        gl_TessLevelOuter[3] = tessLevel;
        gl_TessLevelInner[0] = tessLevel;
        gl_TessLevelInner[1] = tessLevel;
    }
}
)glsl";

// Tessellation Evaluation Shader
const char* tesSource = R"glsl(
#version 410 core
layout (quads, fractional_even_spacing, ccw) in;
uniform sampler2D heightMap;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
in vec2 TextureCoord[];
out float Height;
void main() {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;
    vec4 p00 = gl_in[0].gl_Position;
    vec4 p01 = gl_in[1].gl_Position;
    vec4 p10 = gl_in[2].gl_Position;
    vec4 p11 = gl_in[3].gl_Position;
    vec4 p0 = mix(p00, p01, u);
    vec4 p1 = mix(p10, p11, u);
    vec4 p = mix(p0, p1, v);

    vec2 t00 = TextureCoord[0]; vec2 t01 = TextureCoord[1];
    vec2 t10 = TextureCoord[2]; vec2 t11 = TextureCoord[3];
    vec2 texCoord = mix(mix(t00, t01, u), mix(t10, t11, u), v);

    float h = texture(heightMap, texCoord).r;
    Height = h;
    p.y += h * 2.0;
    gl_Position = projection * view * model * p;
}
)glsl";

// Geometry Shader
const char* geometryShaderSource = R"glsl(
#version 410 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 9) out;
in float Height[];
out float HeightOut;
out vec3 vertColor;
uniform bool showNormals;
void main() {
    for (int i = 0; i < 3; i++) {
        gl_Position = gl_in[i].gl_Position;
        HeightOut = Height[i];
        vertColor = vec3(0.0);
        EmitVertex();
    }
    EndPrimitive();

    if (showNormals) {
        vec3 p0 = gl_in[0].gl_Position.xyz;
        vec3 p1 = gl_in[1].gl_Position.xyz;
        vec3 p2 = gl_in[2].gl_Position.xyz;
        vec3 center = (p0 + p1 + p2) / 3.0;
        vec3 normal = normalize(cross(p1 - p0, p2 - p0));
        float len = 0.5;

        gl_Position = vec4(center, gl_in[0].gl_Position.w); vertColor = vec3(0.0, 1.0, 0.0); EmitVertex();
        gl_Position = vec4(center + normal * len, gl_in[0].gl_Position.w); vertColor = vec3(1.0, 0.0, 0.0); EmitVertex();
        gl_Position = vec4(center + normal * len + vec3(0.001, 0.0, 0.0), gl_in[0].gl_Position.w); vertColor = vec3(1.0, 0.0, 0.0); EmitVertex();
        EndPrimitive();
    }
}
)glsl";

// Fragment Shader
const char* fragmentShaderSource = R"glsl(
#version 410 core
out vec4 FragColor;
in float HeightOut;
in vec3 vertColor;
uniform bool showNormals;
void main() {
    if (showNormals) {
        FragColor = vec4(vertColor, 1.0);
    } else {
        FragColor = vec4(mix(vec3(0.2, 0.6, 0.2), vec3(0.9, 0.9, 0.9), HeightOut), 1.0);
    }
}
)glsl";

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cerr << "SHADER COMPILATION ERROR: " << infoLog << std::endl;
        return 0;
    }
    return shader;
}

// Процедурная генерация карты высот 256×256 (синусы/косинусы)
GLuint generateHeightMap() {
    const int width = 256;
    const int height = 256;
    std::vector<unsigned char> data(width * height);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float nx = static_cast<float>(x) / width;
            float ny = static_cast<float>(y) / height;
            // Простой шум на основе синусов и косинусов
            float val = (sin(nx * 10.0f) * cos(ny * 10.0f) + 1.0f) * 0.5f;
            data[y * width + x] = static_cast<unsigned char>(val * 255.0f);
        }
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return tex;
}

glm::vec3 cameraPos = glm::vec3(0.0f, 5.0f, 15.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
float yaw = -90.0f;
float pitch = 0.0f;
bool firstMouse = true;
int lastMouseX = 0, lastMouseY = 0;

int main() {
    sf::ContextSettings settings;
    settings.depthBits = 24;
    settings.stencilBits = 8;
    settings.antialiasingLevel = 4;
    settings.majorVersion = 4;
    settings.minorVersion = 1;
    settings.attributeFlags = sf::ContextSettings::Core;

    sf::Window window(sf::VideoMode(1280, 720), "Lab 8: GPU Tessellation", sf::Style::Default, settings);
    window.setVerticalSyncEnabled(true);

    if (glewInit() != GLEW_OK) { std::cerr << "Failed to initialize GLEW" << std::endl; return -1; }

    GLuint vShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint tcShader = compileShader(GL_TESS_CONTROL_SHADER, tcsSource);
    GLuint teShader = compileShader(GL_TESS_EVALUATION_SHADER, tesSource);
    GLuint gShader = compileShader(GL_GEOMETRY_SHADER, geometryShaderSource);
    GLuint fShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    GLuint program = glCreateProgram();
    glAttachShader(program, vShader); glAttachShader(program, tcShader); glAttachShader(program, teShader);
    glAttachShader(program, gShader); glAttachShader(program, fShader);
    glLinkProgram(program);

    int linkSuccess;
    glGetProgramiv(program, GL_LINK_STATUS, &linkSuccess);
    if (!linkSuccess) {
        char infoLog[512]; glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "PROGRAM LINK ERROR: " << infoLog << std::endl;
    }

    glDeleteShader(vShader); glDeleteShader(tcShader); glDeleteShader(teShader);
    glDeleteShader(gShader); glDeleteShader(fShader);

    float vertices[] = {
        -5.0f, 0.0f, -5.0f,  0.0f, 0.0f,
         5.0f, 0.0f, -5.0f,  1.0f, 0.0f,
        -5.0f, 0.0f,  5.0f,  0.0f, 1.0f,
         5.0f, 0.0f,  5.0f,  1.0f, 1.0f
    };

    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Генерируем процедурную карту высот вместо загрузки из файла
    GLuint heightMapTex = generateHeightMap();

    glEnable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    window.setMouseCursorGrabbed(true);
    window.setMouseCursorVisible(false);

    bool running = true;
    bool showNormals = false;
    bool nPressed = false;

    // Кешируем локации uniform'ов для скорости
    GLint locProj = glGetUniformLocation(program, "projection");
    GLint locView = glGetUniformLocation(program, "view");
    GLint locModel = glGetUniformLocation(program, "model");
    GLint locCamPos = glGetUniformLocation(program, "cameraPos");
    GLint locShowNorm = glGetUniformLocation(program, "showNormals");
    GLint locHeightMap = glGetUniformLocation(program, "heightMap");

    while (running) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) running = false;
            else if (event.type == sf::Event::Resized) glViewport(0, 0, event.size.width, event.size.height);
            else if (event.type == sf::Event::MouseMoved) {
                if (firstMouse) {
                    lastMouseX = event.mouseMove.x; lastMouseY = event.mouseMove.y;
                    firstMouse = false;
                }
                float xoff = event.mouseMove.x - lastMouseX;
                float yoff = lastMouseY - event.mouseMove.y;
                lastMouseX = event.mouseMove.x; lastMouseY = event.mouseMove.y;
                float sens = 0.1f;
                yaw += xoff * sens; pitch += yoff * sens;
                if (pitch > 89.0f) pitch = 89.0f;
                if (pitch < -89.0f) pitch = -89.0f;
                glm::vec3 front;
                front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
                front.y = sin(glm::radians(pitch));
                front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
                cameraFront = glm::normalize(front);
            }
            else if (event.type == sf::Event::KeyPressed) {
                if (event.key.code == sf::Keyboard::Escape) {
                    running = false; // Выход по Esc
                }
                if (event.key.code == sf::Keyboard::N && !nPressed) {
                    showNormals = !showNormals;
                    nPressed = true;
                }
            }
            else if (event.type == sf::Event::KeyReleased) {
                if (event.key.code == sf::Keyboard::N) nPressed = false;
            }
        }

        // Центрируем мышь для плавного вращения
        sf::Vector2i center(window.getSize().x / 2, window.getSize().y / 2);
        sf::Mouse::setPosition(center, window);
        lastMouseX = center.x; lastMouseY = center.y;

        float speed = sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) ? 0.25f : 0.05f;
        glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));
        glm::vec3 up = glm::normalize(glm::cross(right, cameraFront));

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::W)) cameraPos += speed * cameraFront;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::S)) cameraPos -= speed * cameraFront;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::A)) cameraPos -= speed * right;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::D)) cameraPos += speed * right;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Space)) cameraPos += speed * up;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::LControl)) cameraPos -= speed * up;

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(program);

        glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)window.getSize().x / window.getSize().y, 0.1f, 100.0f);
        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        glm::mat4 model(1.0f);

        glUniformMatrix4fv(locProj, 1, GL_FALSE, glm::value_ptr(proj));
        glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(model));
        glUniform3fv(locCamPos, 1, glm::value_ptr(cameraPos));
        glUniform1i(locShowNorm, showNormals ? 1 : 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, heightMapTex);
        glUniform1i(locHeightMap, 0);

        glBindVertexArray(VAO);
        glPatchParameteri(GL_PATCH_VERTICES, 4);
        glDrawArrays(GL_PATCHES, 0, 4);

        window.display();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(program);
    glDeleteTextures(1, &heightMapTex);
    return 0;
}