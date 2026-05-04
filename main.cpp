#include <GL/glew.h>
#include <SFML/Window.hpp>
#include <SFML/OpenGL.hpp>
#include <iostream>
#include <vector>
#include <cmath>

// Математические функции для простых перемещений, если нет GLM
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

void main() {
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    TextureCoord[gl_InvocationID] = TexCoord[gl_InvocationID];

    if(gl_InvocationID == 0) {
        // Тесселяция 10x10 для квада
        gl_TessLevelOuter[0] = 10.0;
        gl_TessLevelOuter[1] = 10.0;
        gl_TessLevelOuter[2] = 10.0;
        gl_TessLevelOuter[3] = 10.0;

        gl_TessLevelInner[0] = 10.0;
        gl_TessLevelInner[1] = 10.0;
    }
}
)glsl";

// Tessellation Evaluation Shader
const char* tesSource = R"glsl(
#version 410 core
layout (quads, fractional_odd_spacing, ccw) in;

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

    vec2 t00 = TextureCoord[0];
    vec2 t01 = TextureCoord[1];
    vec2 t10 = TextureCoord[2];
    vec2 t11 = TextureCoord[3];

    vec2 t0 = mix(t00, t01, u);
    vec2 t1 = mix(t10, t11, u);
    vec2 texCoord = mix(t0, t1, v);

    float height = texture(heightMap, texCoord).r;
    Height = height;

    // Смещение по Y (вверх) на основе карты высот
    p.y += height * 2.0;

    gl_Position = projection * view * model * p;
}
)glsl";

// Fragment Shader
const char* fragmentShaderSource = R"glsl(
#version 410 core
out vec4 FragColor;
in float Height;
void main() {
    // Простая раскраска на основе высоты
    vec3 color = mix(vec3(0.2, 0.6, 0.2), vec3(0.9, 0.9, 0.9), Height);
    FragColor = vec4(color, 1.0);
}
)glsl";

// Компиляция шейдеров
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cout << "SHADER COMPILATION ERROR\n" << infoLog << std::endl;
    }
    return shader;
}

// Генерация текстурной карты высот (процедурный шум)
GLuint generateHeightMap() {
    const int width = 256;
    const int height = 256;
    std::vector<unsigned char> data(width * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float nx = (float)x / width;
            float ny = (float)y / height;
            // Простой шум на основе синусов
            float val = (sin(nx * 10.0f) * cos(ny * 10.0f) + 1.0f) * 0.5f;
            data[y * width + x] = static_cast<unsigned char>(val * 255.0f);
        }
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return texture;
}

// Глобальные переменные для камеры
float cameraAngleX = 0.0f;
float cameraAngleY = 0.0f;
bool isDragging = false;
int lastMouseX, lastMouseY;

int main() {
    sf::ContextSettings settings;
    settings.depthBits = 24;
    settings.stencilBits = 8;
    settings.antialiasingLevel = 4;
    settings.majorVersion = 4;
    settings.minorVersion = 1;
    settings.attributeFlags = sf::ContextSettings::Core;

    sf::Window window(sf::VideoMode(800, 600), "Lab 8: GPU Tessellation with SFML", sf::Style::Default, settings);
    window.setVerticalSyncEnabled(true);

    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }

    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint tcsShader = compileShader(GL_TESS_CONTROL_SHADER, tcsSource);
    GLuint tesShader = compileShader(GL_TESS_EVALUATION_SHADER, tesSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, tcsShader);
    glAttachShader(shaderProgram, tesShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(tcsShader);
    glDeleteShader(tesShader);
    glDeleteShader(fragmentShader);

    // Вершины патча (квадрат 10x10)
    float vertices[] = {
        // x, y, z,          u, v
       -5.0f, 0.0f, -5.0f,   0.0f, 0.0f,
        5.0f, 0.0f, -5.0f,   1.0f, 0.0f,
       -5.0f, 0.0f,  5.0f,   0.0f, 1.0f,
        5.0f, 0.0f,  5.0f,   1.0f, 1.0f
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

    GLuint heightMapTex = generateHeightMap();

    glEnable(GL_DEPTH_TEST);
    
    // Включаем отображение сетки для демонстрации тесселяции
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    bool running = true;
    while (running) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                running = false;
            }
            else if (event.type == sf::Event::Resized) {
                glViewport(0, 0, event.size.width, event.size.height);
            }
            else if (event.type == sf::Event::MouseButtonPressed) {
                if (event.mouseButton.button == sf::Mouse::Left) {
                    isDragging = true;
                    lastMouseX = event.mouseButton.x;
                    lastMouseY = event.mouseButton.y;
                }
            }
            else if (event.type == sf::Event::MouseButtonReleased) {
                if (event.mouseButton.button == sf::Mouse::Left) {
                    isDragging = false;
                }
            }
            else if (event.type == sf::Event::MouseMoved) {
                if (isDragging) {
                    int dx = event.mouseMove.x - lastMouseX;
                    int dy = event.mouseMove.y - lastMouseY;
                    cameraAngleX += dx * 0.01f;
                    cameraAngleY += dy * 0.01f;
                    
                    if (cameraAngleY > 1.5f) cameraAngleY = 1.5f;
                    if (cameraAngleY < -0.1f) cameraAngleY = -0.1f;
                    
                    lastMouseX = event.mouseMove.x;
                    lastMouseY = event.mouseMove.y;
                }
            }
        }

        // Управление с клавиатуры (WASD и стрелочки)
        float keySpeed = 0.02f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::A) || sf::Keyboard::isKeyPressed(sf::Keyboard::Left)) {
            cameraAngleX -= keySpeed;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::D) || sf::Keyboard::isKeyPressed(sf::Keyboard::Right)) {
            cameraAngleX += keySpeed;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::W) || sf::Keyboard::isKeyPressed(sf::Keyboard::Up)) {
            cameraAngleY -= keySpeed;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::S) || sf::Keyboard::isKeyPressed(sf::Keyboard::Down)) {
            cameraAngleY += keySpeed;
        }

        // Ограничитель вращения после изменения клавишами
        if (cameraAngleY > 1.5f) cameraAngleY = 1.5f;
        if (cameraAngleY < -0.1f) cameraAngleY = -0.1f;

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);

        glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)window.getSize().x / (float)window.getSize().y, 0.1f, 100.0f);
        
        glm::vec3 cameraPos = glm::vec3(sin(cameraAngleX) * 15.0f, 5.0f + cameraAngleY * 10.0f, cos(cameraAngleX) * 15.0f);
        glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 model = glm::mat4(1.0f);

        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, heightMapTex);
        glUniform1i(glGetUniformLocation(shaderProgram, "heightMap"), 0);

        glBindVertexArray(VAO);
        glPatchParameteri(GL_PATCH_VERTICES, 4);
        glDrawArrays(GL_PATCHES, 0, 4);

        window.display();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);
    glDeleteTextures(1, &heightMapTex);

    return 0;
}