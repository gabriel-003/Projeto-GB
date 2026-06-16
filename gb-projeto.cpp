#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "gl_utils.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctime>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <utility>
#include "TileMap.h"
#include "DiamondView.h"
#include "ltMath.h"

/* ============================================================
   compilar:
   g++ -o gb-projeto gl_utils.cpp maths_funcs.cpp stb_image.cpp gb-projeto.cpp -lGL -lGLEW -lglfw
   ============================================================ */

using namespace std;

int g_gl_width  = 800;
int g_gl_height = 800;
GLFWwindow *g_window = NULL;

// coordenadas normalizadas da tela (-1..1)
float xi = -1.0f, xf = 1.0f;
float yi = -1.0f, yf = 1.0f;
float w = 2.0f, h = 2.0f;
float tw, th, tw2, th2;          

// layout do tileset 
int tileSetCols = 1, tileSetRows = 1;
float tileW, tileH, tileW2, tileH2;

// camera (segue o jogador para que o mapa 15x15 caiba sempre na tela)
float camX = 0.0f, camY = 0.0f;

TilemapView *tview = new DiamondView();
TileMap     *tmap  = NULL;

enum GameState { PLAYING, WIN, LOSE };
GameState gameState = PLAYING;

//  Estrutura que guarda tudo que veio do arquivo de configuração
struct MapConfig {
    string tilesetFile = "tilesetIso.png";
    int    numTiles = 1, tilePxW = 1, tilePxH = 1;
    vector<int> walkable;            // walkable[id] : 1 = pode pisar
    vector<int> deadly;              // deadly[id]   : 1 = mata o jogador
    int    visitedTile = -1;         // id do tile usado na troca (rastro)
    int    mapW = 0, mapH = 0;
    vector<vector<int>> tiles;       // tiles[row][col]
    int    playerCol = 0, playerRow = 0;
    vector<pair<int,int>> coins;     // (col,row) de cada moeda
    int    goalCol = -1, goalRow = -1;
};
MapConfig cfg;

// estado de jogo
struct Player {
    int col, row;
    int frameAtual  = 0;
    int totalFrames = 12;
    double lastFrameTime = 0.0;
    double frameInterval = 0.1;
};
Player player;

struct Coin { int col, row; bool collected = false; };
vector<Coin> coins;
int coinsCollected = 0;

// texturas e buffers
GLuint tidTerrain, tidPlayer, tidCoin;
GLuint VAO_terrain, VBO_terrain, EBO_terrain;
GLuint VAO_sprite,  VBO_sprite,  EBO_sprite;
GLuint shader_programme;

//  leitura do arquivo de configuração
MapConfig readConfig(const char *filename) {
    MapConfig c;
    ifstream arq(filename);
    if (!arq.is_open()) {
        cout << "ERRO: nao foi possivel abrir " << filename << endl;
        return c;
    }
    string line;
    while (getline(arq, line)) {
        // tudo depois de '#' e comentario
        size_t hash = line.find('#');
        if (hash != string::npos) line = line.substr(0, hash);

        stringstream ss(line);
        string key;
        if (!(ss >> key)) continue;                 // linha em branco

        if (key == "TILESET") {
            ss >> c.tilesetFile >> c.numTiles >> c.tilePxW >> c.tilePxH;
        } else if (key == "WALKABLE") {
            c.walkable.clear(); int v; while (ss >> v) c.walkable.push_back(v);
        } else if (key == "DEADLY") {
            c.deadly.clear();   int v; while (ss >> v) c.deadly.push_back(v);
        } else if (key == "VISITED") {
            ss >> c.visitedTile;
        } else if (key == "SIZE") {
            ss >> c.mapW >> c.mapH;
        } else if (key == "PLAYER") {
            ss >> c.playerCol >> c.playerRow;
        } else if (key == "GOAL") {
            ss >> c.goalCol >> c.goalRow;
        } else if (key == "COIN") {
            int cc, rr; ss >> cc >> rr; c.coins.push_back({cc, rr});
        } else if (key == "MAP") {
            // matriz baseada na key map
            c.tiles.assign(c.mapH, vector<int>(c.mapW, 0));
            for (int r = 0; r < c.mapH; r++)
                for (int col = 0; col < c.mapW; col++)
                    arq >> c.tiles[r][col];
            getline(arq, line); 
        }
    }
    arq.close();
    return c;
}

// consultas de propriedade do tile vindas do config
bool tileWalkable(int id) {
    if (id < 0 || id >= (int)cfg.walkable.size()) return false;
    return cfg.walkable[id] == 1;
}
bool tileDeadly(int id) {
    if (id < 0 || id >= (int)cfg.deadly.size()) return false;
    return cfg.deadly[id] == 1;
}

// so podemos entrar num tile caminhavel e dentro do mapa.
// (a lava e "caminhavel" de proposito: o player pode pisar e morrer)
bool posicaoValida(int col, int row) {
    if (col < 0 || col >= tmap->getWidth())  return false;
    if (row < 0 || row >= tmap->getHeight()) return false;
    return tileWalkable((int)tmap->getTile(col, row));
}

void loadTexture(GLuint &texture, const char *filename, int *outW = nullptr, int *outH = nullptr) {
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    int width, height, nrChannels;
    unsigned char *data = stbi_load(filename, &width, &height, &nrChannels, 0);
    if (data) {
        GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        if (outW) *outW = width;
        if (outH) *outH = height;
    } else {
        cout << "ERRO ao carregar textura: " << filename << endl;
    }
    stbi_image_free(data);
}

//  Desenha player ou moeda na posicao do tile
void drawSprite(GLuint texId, int col, int row,
                int frameCol, int frameRow,
                int totalFrameCols, int totalFrameRows,
                float scale = 1.0f)
{
    float tx, ty;
    tview->computeDrawPosition(col, row, tw, th, tx, ty);

    float u0 = (float) frameCol      / (float)totalFrameCols;
    float u1 = (float)(frameCol + 1) / (float)totalFrameCols;
    float v0 = (float) frameRow      / (float)totalFrameRows;
    float v1 = (float)(frameRow + 1) / (float)totalFrameRows;
    float offset = tw * (1.0f - scale) / 2.0f;
    float verts[] = {
        xi+offset,    yi+th,           u0, v1,
        xi+tw-offset, yi+th,           u1, v1,
        xi+tw-offset, yi+th+tw*scale,  u1, v0,
        xi+offset,    yi+th+tw*scale,  u0, v0,
    };
    glBindVertexArray(VAO_sprite);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_sprite);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texId);
    glUniform1f(glGetUniformLocation(shader_programme, "offsetx"), 0.0f);
    glUniform1f(glGetUniformLocation(shader_programme, "offsety"), 0.0f);
    glUniform1f(glGetUniformLocation(shader_programme, "tx"), tx - camX + 1.0f);
    glUniform1f(glGetUniformLocation(shader_programme, "ty"), ty - camY + 1.0f);
    glUniform1f(glGetUniformLocation(shader_programme, "weight"), 0.0f);
    glUniform1i(glGetUniformLocation(shader_programme, "sprite"), 0);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}


//  logica de movimento/coleta/morte/vitoria
void handlePlayerInput(double currentTime) {
    if (gameState != PLAYING) return;
    static double lastMove = 0.0;
    if (currentTime - lastMove < 0.18) return;      // taxa de passo

    int nc = player.col, nr = player.row;
    bool moved = true;

    // 4 direcoes cardeais
    if      (glfwGetKey(g_window, GLFW_KEY_W)==GLFW_PRESS || glfwGetKey(g_window, GLFW_KEY_UP)==GLFW_PRESS    || glfwGetKey(g_window, GLFW_KEY_KP_8)==GLFW_PRESS)
        tview->computeTileWalking(nc, nr, DIRECTION_NORTH);
    else if (glfwGetKey(g_window, GLFW_KEY_S)==GLFW_PRESS || glfwGetKey(g_window, GLFW_KEY_DOWN)==GLFW_PRESS  || glfwGetKey(g_window, GLFW_KEY_KP_2)==GLFW_PRESS)
        tview->computeTileWalking(nc, nr, DIRECTION_SOUTH);
    else if (glfwGetKey(g_window, GLFW_KEY_A)==GLFW_PRESS || glfwGetKey(g_window, GLFW_KEY_LEFT)==GLFW_PRESS  || glfwGetKey(g_window, GLFW_KEY_KP_4)==GLFW_PRESS)
        tview->computeTileWalking(nc, nr, DIRECTION_WEST);
    else if (glfwGetKey(g_window, GLFW_KEY_D)==GLFW_PRESS || glfwGetKey(g_window, GLFW_KEY_RIGHT)==GLFW_PRESS || glfwGetKey(g_window, GLFW_KEY_KP_6)==GLFW_PRESS)
        tview->computeTileWalking(nc, nr, DIRECTION_EAST);
    // 4 diagonais
    else if (glfwGetKey(g_window, GLFW_KEY_E)==GLFW_PRESS || glfwGetKey(g_window, GLFW_KEY_KP_9)==GLFW_PRESS)
        tview->computeTileWalking(nc, nr, DIRECTION_NORTHEAST);
    else if (glfwGetKey(g_window, GLFW_KEY_Q)==GLFW_PRESS || glfwGetKey(g_window, GLFW_KEY_KP_7)==GLFW_PRESS)
        tview->computeTileWalking(nc, nr, DIRECTION_NORTHWEST);
    else if (glfwGetKey(g_window, GLFW_KEY_C)==GLFW_PRESS || glfwGetKey(g_window, GLFW_KEY_KP_3)==GLFW_PRESS)
        tview->computeTileWalking(nc, nr, DIRECTION_SOUTHEAST);
    else if (glfwGetKey(g_window, GLFW_KEY_Z)==GLFW_PRESS || glfwGetKey(g_window, GLFW_KEY_KP_1)==GLFW_PRESS)
        tview->computeTileWalking(nc, nr, DIRECTION_SOUTHWEST);
    else
        moved = false;

    if (!moved || !posicaoValida(nc, nr)) return;
    player.col = nc;
    player.row = nr;
    lastMove   = currentTime;

    int destId = (int)tmap->getTile(player.col, player.row);

    // Derrota, pisou em lava
    if (tileDeadly(destId)) {
        gameState = LOSE;
        cout << "DERROTA! Voce caiu na lava." << endl;
        return;
    }

    // Coleta de moeda
    for (auto &m : coins) {
        if (!m.collected && m.col == player.col && m.row == player.row) {
            m.collected = true;
            coinsCollected++;
            cout << "Moeda " << coinsCollected << "/" << (int)coins.size() << endl;
        }
    }

    // vitoria ao coletar moedas
    if (coinsCollected >= (int)coins.size()) {
        gameState = WIN;
        cout << "VITORIA! Todas as moedas coletadas!" << endl;
        return; // nao troca o tile nem processa mais nada
    }

    // troca de tile com rasto do player
    if (cfg.visitedTile >= 0 &&
        destId != cfg.visitedTile && !tileDeadly(destId)) {
        tmap->setTile(player.col, player.row, cfg.visitedTile);
    }
}

//  inicia o jogo recriando o mapa a partir do config.
//  recriar o TileMap garante que o rastro some no restart
void initGame() {
    gameState = PLAYING;
    coinsCollected = 0;

    delete tmap;
    tmap = new TileMap(cfg.mapW, cfg.mapH, 0);
    for (int r = 0; r < cfg.mapH; r++)
        for (int c = 0; c < cfg.mapW; c++)
            tmap->setTile(c, r, cfg.tiles[r][c]);
    tmap->setTid(tidTerrain);

    player.col = cfg.playerCol;
    player.row = cfg.playerRow;
    player.frameAtual = 0;

    coins.clear();
    for (auto &p : cfg.coins) {
        Coin m; m.col = p.first; m.row = p.second; m.collected = false;
        coins.push_back(m);
    }

    cout << "\n=== GB: Caça ao Tesouro ===" << endl;
    cout << "WASD/setas: N/S/O/L | Q/E/Z/C: diagonais | R: reiniciar | ESC: sair" << endl;
    cout << "Colete as " << (int)coins.size()
         << " moedas e chegue na saida. Cuidado com a lava!\n" << endl;
}

int main() {
    restart_gl_log();
    start_gl();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // le o config - mapa, walkability e objetos
    cfg = readConfig("mapa.txt");
    if (cfg.mapW < 15 || cfg.mapH < 15)
        cout << "AVISO: o mapa deveria ter no minimo 15x15." << endl;

    
    tw  = w / (float)cfg.mapW;
    th  = tw / 2.0f;             // proporcao 2:1 do diamond
    tw2 = th;  th2 = th / 2.0f;

    // carrega tileset e descobre o layout (cols x rows) pela imagem
    int imgW = 0, imgH = 0;
    loadTexture(tidTerrain, cfg.tilesetFile.c_str(), &imgW, &imgH);
    tileSetCols = (cfg.tilePxW > 0) ? imgW / cfg.tilePxW : cfg.numTiles;
    tileSetRows = (cfg.tilePxH > 0) ? imgH / cfg.tilePxH : 1;
    if (tileSetCols < 1) tileSetCols = cfg.numTiles;
    if (tileSetRows < 1) tileSetRows = 1;
    tileW  = 1.0f / (float)tileSetCols;
    tileH  = 1.0f / (float)tileSetRows;
    tileW2 = tileW / 2.0f;
    tileH2 = tileH / 2.0f;

    loadTexture(tidPlayer, "player.png");
    loadTexture(tidCoin,   "coin.png");

    initGame();

    // VAO do terreno (losango / diamond)
    float vertices_terrain[] = {
        xi,      yi+th2,  0.0f,   tileH2,
        xi+tw2,  yi,      tileW2, 0.0f,
        xi+tw,   yi+th2,  tileW,  tileH2,
        xi+tw2,  yi+th,   tileW2, tileH,
    };
    unsigned int indices[] = { 0, 1, 3, 3, 1, 2 };
    glGenVertexArrays(1, &VAO_terrain);
    glGenBuffers(1, &VBO_terrain);
    glGenBuffers(1, &EBO_terrain);
    glBindVertexArray(VAO_terrain);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_terrain);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices_terrain), vertices_terrain, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_terrain);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);

    // VAO do sprite (retangulo dinamico, atualizado por frame)
    float vertices_sprite[16] = {0};
    unsigned int indices_sprite[] = { 0, 1, 2, 0, 2, 3 };
    glGenVertexArrays(1, &VAO_sprite);
    glGenBuffers(1, &VBO_sprite);
    glGenBuffers(1, &EBO_sprite);
    glBindVertexArray(VAO_sprite);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_sprite);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices_sprite), vertices_sprite, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_sprite);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices_sprite), indices_sprite, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);

    // shaders
    char vertex_shader[1024*256];
    char fragment_shader[1024*256];
    parse_file_into_str("_geral_vs.glsl", vertex_shader,   1024*256);
    parse_file_into_str("_geral_fs.glsl", fragment_shader, 1024*256);
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    const GLchar *p = (const GLchar*)vertex_shader;
    glShaderSource(vs, 1, &p, NULL); glCompileShader(vs);
    int params = -1; glGetShaderiv(vs, GL_COMPILE_STATUS, &params);
    if (GL_TRUE != params) { print_shader_info_log(vs); return 1; }
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    p = (const GLchar*)fragment_shader;
    glShaderSource(fs, 1, &p, NULL); glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &params);
    if (GL_TRUE != params) { print_shader_info_log(fs); return 1; }
    shader_programme = glCreateProgram();
    glAttachShader(shader_programme, fs);
    glAttachShader(shader_programme, vs);
    glLinkProgram(shader_programme);
    glGetProgramiv(shader_programme, GL_LINK_STATUS, &params);
    if (GL_TRUE != params) { cerr << "Erro ao linkar shader" << endl; return 1; }

    static bool rWasPressed = false;

    while (!glfwWindowShouldClose(g_window)) {
        _update_fps_counter(g_window);
        double currentTime = glfwGetTime();

        handlePlayerInput(currentTime);

        // animacao do player
        if (currentTime - player.lastFrameTime > player.frameInterval) {
            player.frameAtual = (player.frameAtual + 1) % player.totalFrames;
            player.lastFrameTime = currentTime;
        }

        // camera segue o jogador (mantem o mapa 15x15 sempre visivel)
        tview->computeDrawPosition(player.col, player.row, tw, th, camX, camY);

        // cor de fundo conforme estado
        if      (gameState == WIN)  glClearColor(0.0f, 0.45f, 0.10f, 1.0f);
        else if (gameState == LOSE) glClearColor(0.45f, 0.05f, 0.05f, 1.0f);
        else                        glClearColor(0.08f, 0.08f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glViewport(0, 0, g_gl_width, g_gl_height);
        glUseProgram(shader_programme);

        // desenha terreno
        glBindVertexArray(VAO_terrain);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tidTerrain);
        for (int r = 0; r < tmap->getHeight(); r++) {
            for (int c = 0; c < tmap->getWidth(); c++) {
                int t_id = (int)tmap->getTile(c, r);
                int u = t_id % tileSetCols;
                int v = t_id / tileSetCols;
                float tx, ty;
                tview->computeDrawPosition(c, r, tw, th, tx, ty);
                float weight = 0.0f;
                if (c == player.col && r == player.row)         weight = 0.00f;
                else if (c == cfg.goalCol && r == cfg.goalRow)  weight = 0.00f;
                glUniform1f(glGetUniformLocation(shader_programme, "offsetx"), u * tileW);
                glUniform1f(glGetUniformLocation(shader_programme, "offsety"), v * tileH);
                glUniform1f(glGetUniformLocation(shader_programme, "tx"), tx - camX + 1.0f);
                glUniform1f(glGetUniformLocation(shader_programme, "ty"), ty - camY + 1.0f);
                glUniform1f(glGetUniformLocation(shader_programme, "layer_z"), tmap->getZ());
                glUniform1f(glGetUniformLocation(shader_programme, "weight"), weight);
                glUniform1i(glGetUniformLocation(shader_programme, "sprite"), 0);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            }
        }

        // desenha moedas
        for (auto &m : coins)
            if (!m.collected)
                drawSprite(tidCoin, m.col, m.row, 0, 0, 1, 1, 0.4f);

        // desenha player
        drawSprite(tidPlayer, player.col, player.row,
                   player.frameAtual, 0, player.totalFrames, 1);

        // reiniciar (R, borda de subida para nao repetir)
        bool rNow = glfwGetKey(g_window, GLFW_KEY_R) == GLFW_PRESS;
        if (rNow && !rWasPressed) initGame();
        rWasPressed = rNow;

        glfwPollEvents();
        if (glfwGetKey(g_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(g_window, 1);
        glfwSwapBuffers(g_window);
    }

    glfwTerminate();
    delete tmap;
    delete tview;
    return 0;
}