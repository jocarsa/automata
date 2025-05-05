#include <opencv2/opencv.hpp>
#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <ctime>
#include <cmath>
#include <numeric>
#include <random>
#include <iostream>

using namespace cv;
using namespace std;

// ======== Configuración Ajustable ========
struct ConfiguracionJuego {
    // === Física ===
    double gravedad           = 1000.0;   // gravedad del mundo (píxeles/s²)
    double friccionSuelo      = 0.8;      // velocidad horizontal retenida por fotograma cuando en el suelo
    double friccionAire       = 0.98;     // velocidad horizontal retenida por fotograma cuando en el aire
    double velocidadMovimiento= 800.0;    // velocidad de movimiento horizontal (píxeles/s)
    double velocidadSalto     = 950.0;    // impulso inicial de salto (píxeles/s)

    // === Desplazamiento y Niveles ===
    double velocidadDesplazamiento = 10.0;     // velocidad de desplazamiento del fondo (píxeles/s)
    double incrementoVelocidad     = 5.0;      // incremento de velocidadDesplazamiento por nivel
    int    fotogramasCambioColor    = 600;      // fotogramas entre cambios de color (y velocidad)

    // === Autómata Celular ===
    double tasaEspontanea   = 0.002;    // probabilidad por actualización de nacimiento espontáneo de célula

    // === Jugador e Ítems ===
    float  radioJugador      = 40.0f;    // radio de colisión
    float  radioItem         = 25.0f;    // radio de ítem recuperable

    // === Pantalla ===
    int    fotogramasMostrarTitulo = 300;      // fotogramas para mostrar el título
    int    fotogramasMostrarNivel  = 180;      // fotogramas para mostrar “Nivel N”
};

// Convertir cadena de color hex a Scalar de OpenCV (B, G, R)
static Scalar hex2Scalar(const string& hex) {
    int r = stoi(hex.substr(1,2), nullptr, 16);
    int g = stoi(hex.substr(3,2), nullptr, 16);
    int b = stoi(hex.substr(5,2), nullptr, 16);
    return Scalar(b, g, r);
}

struct Celda {
    Point2f centro;
    bool viva, creciendo, encogiendo, zonaSinGeneracion;
    double factorTamano;
    int indiceColor, fotogramasPorActualizacion, cuentaAtrasFotogramas;
};

struct Recuperable {
    Point2f pos, vel;
    bool recolectado;
    float radio;
};

struct Jugador {
    Point2f pos, vel;
    float radio;
    bool enSuelo, mirandoDerecha;
};

int main() {
    // Ventana y temporización
    int ancho  = 1920;
    int alto   = 1080;
    const int fps    = 60;
    const double dt  = 1.0 / fps;

    ConfiguracionJuego cfg;

    const double tamanoCelda = 120.0;
    int columnas = ancho  / (int)tamanoCelda;
    int filas    = alto   / (int)tamanoCelda;

    // Configuración de SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        cerr << "Error SDL_Init: " << SDL_GetError() << endl;
        return 1;
    }
    SDL_Window*   ventana   = SDL_CreateWindow("Plataformas",
                                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                              ancho, alto,
                                              SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderizador = SDL_CreateRenderer(ventana, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture*  textura  = SDL_CreateTexture(renderizador,
                                              SDL_PIXELFORMAT_BGR24,
                                              SDL_TEXTUREACCESS_STREAMING,
                                              ancho, alto);

    // Generador de números aleatorios
    mt19937 rng((unsigned)time(nullptr));
    uniform_real_distribution<double> dist01(0,1);
    uniform_real_distribution<double> distVelocidad(0.5,2.0);

    // Paleta de colores
    std::vector<std::string> coloresHex = {
    "#0000FF", "#8A2BE2", "#A52A2A", "#5F9EA0", "#D2691E", "#FF7F50", "#6495ED", "#DC143C",
    "#00CED1", "#00008B", "#008B8B", "#B8860B", "#006400", "#8B008B", "#556B2F", "#FF8C00",
    "#9932CC", "#8B0000", "#483D8B", "#2F4F4F", "#00CED1", "#9400D3", "#FF1493", "#00BFFF",
    "#696969", "#1E90FF", "#B22222", "#228B22", "#FF00FF", "#808080", "#008000", "#FF69B4",
    "#CD5C5C", "#4B0082", "#F08080", "#20B2AA", "#778899", "#00FF00", "#32CD32", "#FF00FF",
    "#800000", "#0000CD", "#BA55D3", "#9370DB", "#3CB371", "#7B68EE", "#C71585", "#191970",
    "#000080", "#808000", "#6B8E23", "#FF4500", "#DA70D6", "#DB7093", "#CD853F", "#800080",
    "#663399", "#FF0000", "#BC8F8F", "#4169E1", "#8B4513", "#FA8072", "#2E8B57", "#A0522D",
    "#6A5ACD", "#708090", "#4682B4", "#008080", "#FF6347"
    };

    bool salirTodo = false;
    while (!salirTodo) {
        // Mezclar los índices de la paleta
        int totalColores = (int)coloresHex.size();
        vector<int> paleta(totalColores);
        iota(paleta.begin(), paleta.end(), 0);
        shuffle(paleta.begin(), paleta.end(), rng);
        int indiceCicloColor = 0;

        // Inicializar celdas
        vector<Celda> celdas;
        celdas.reserve(columnas * filas);
        for (int y = 0; y < filas; ++y) {
            for (int x = 0; x < columnas; ++x) {
                Celda c;
                c.centro = Point2f(x * tamanoCelda + tamanoCelda/2,
                                   y * tamanoCelda + tamanoCelda/2);
                bool zonaCentral = fabs(c.centro.x - ancho/2) < tamanoCelda
                               && fabs(c.centro.y - alto/2) < tamanoCelda;
                c.zonaSinGeneracion = zonaCentral;
                c.viva       = !zonaCentral && dist01(rng) > 0.5;
                c.factorTamano  = c.viva ? 1.0 : 0.0;
                c.creciendo     = c.viva;
                c.encogiendo   = false;
                c.indiceColor    = paleta[indiceCicloColor];
                double velocidad  = distVelocidad(rng);
                c.fotogramasPorActualizacion = max(1, (int)round(fps / velocidad));
                c.cuentaAtrasFotogramas  = c.fotogramasPorActualizacion;
                celdas.push_back(c);
            }
        }

        // Ítems recuperables
        vector<Recuperable> items;

        // Jugador
        Jugador jugador;
        jugador.pos        = Point2f(ancho/2, alto/2);
        jugador.vel        = Point2f(0,0);
        jugador.radio     = cfg.radioJugador;
        jugador.enSuelo   = false;
        jugador.mirandoDerecha= true;

        Mat fotograma(alto, ancho, CV_8UC3);
        int cuentaGlobalFotogramas    = 0;
        int nivel               = 1;
        int cuentaMostrarNivel   = cfg.fotogramasMostrarNivel;
        bool mostrarTitulo          = true;
        int cuentaMostrarTitulo   = cfg.fotogramasMostrarTitulo;
        int puntuacion               = 0;
        bool salir               = false;
        double velocidadDesplazamiento      = cfg.velocidadDesplazamiento;

        while (!salir) {
            // ===== Evento e Entrada =====
            SDL_Event evento;
            while (SDL_PollEvent(&evento)) {
                if (evento.type == SDL_QUIT) {
                    salir    = true;
                    salirTodo = true;
                } else if (evento.type == SDL_WINDOWEVENT && evento.window.event == SDL_WINDOWEVENT_RESIZED) {
                    // Manejar redimensionamiento de ventana
                    ancho  = evento.window.data1;
                    alto   = evento.window.data2;
                    columnas = ancho  / (int)tamanoCelda;
                    filas    = alto   / (int)tamanoCelda;
                    SDL_DestroyTexture(textura);
                    textura = SDL_CreateTexture(renderizador,
                                                SDL_PIXELFORMAT_BGR24,
                                                SDL_TEXTUREACCESS_STREAMING,
                                                ancho, alto);
                    fotograma = Mat(alto, ancho, CV_8UC3);
                }
            }
            const Uint8* teclas = SDL_GetKeyboardState(NULL);
            if (teclas[SDL_SCANCODE_ESCAPE]) {
                salir    = true;
                salirTodo = true;
            }
            bool izquierda  = teclas[SDL_SCANCODE_A] || teclas[SDL_SCANCODE_LEFT];
            bool derecha = teclas[SDL_SCANCODE_D] || teclas[SDL_SCANCODE_RIGHT];
            bool salto  = teclas[SDL_SCANCODE_W] || teclas[SDL_SCANCODE_UP];

            // Movimiento horizontal y dirección
            if (izquierda) {
                jugador.vel.x      = -cfg.velocidadMovimiento;
                jugador.mirandoDerecha = false;
            }
            else if (derecha) {
                jugador.vel.x      = +cfg.velocidadMovimiento;
                jugador.mirandoDerecha = true;
            }

            // Aplicar gravedad
            jugador.vel.y += cfg.gravedad * dt;

            // Fricción
            if (jugador.enSuelo) jugador.vel.x *= cfg.friccionSuelo;
            else                jugador.vel.x *= cfg.friccionAire;

            // Desplazar mundo
            for (auto& c : celdas)   c.centro.x -= velocidadDesplazamiento * dt;
            for (auto& itm : items) itm.pos.x    -= velocidadDesplazamiento * dt;
            jugador.pos.x            -= velocidadDesplazamiento * dt;

            // Aplicar velocidad
            jugador.pos += jugador.vel * dt;

            // ===== Reciclar celdas y generar ítems =====
            vector<Celda> nuevasCeldas;
            for (auto it = celdas.begin(); it != celdas.end();) {
                if (it->centro.x + tamanoCelda/2 < 0) {
                    int yIdx = (int)(it->centro.y / tamanoCelda);
                    // Nueva celda a la derecha
                    Celda c;
                    c.centro = Point2f(ancho + tamanoCelda/2,
                                       yIdx*tamanoCelda + tamanoCelda/2);
                    c.zonaSinGeneracion     = false;
                    c.viva           = dist01(rng) > 0.5;
                    c.factorTamano      = c.viva ? 1.0 : 0.0;
                    c.creciendo         = c.viva;
                    c.encogiendo       = false;
                    c.indiceColor        = paleta[indiceCicloColor];
                    double velocidad      = distVelocidad(rng);
                    c.fotogramasPorActualizacion = max(1, (int)round(fps / velocidad));
                    c.cuentaAtrasFotogramas  = c.fotogramasPorActualizacion;
                    nuevasCeldas.push_back(c);

                    // Generar ítem recuperable
                    Recuperable itm;
                    itm.pos       = Point2f(ancho + tamanoCelda/2, yIdx*tamanoCelda);
                    itm.vel       = Point2f(0,0);
                    itm.recolectado = false;
                    itm.radio    = cfg.radioItem;
                    items.push_back(itm);

                    it = celdas.erase(it);
                } else {
                    ++it;
                }
            }
            celdas.insert(celdas.end(), nuevasCeldas.begin(), nuevasCeldas.end());

            // ===== Ciclo de nivel y color =====
            if (cuentaGlobalFotogramas > 0
             && cuentaGlobalFotogramas % cfg.fotogramasCambioColor == 0) {
                indiceCicloColor = (indiceCicloColor + 1) % totalColores;
                nivel++;
                velocidadDesplazamiento += cfg.incrementoVelocidad;
                cuentaMostrarNivel = cfg.fotogramasMostrarNivel;
            }
            int colorActual = paleta[indiceCicloColor];

            // ===== Dibujar fondo =====
            fotograma.setTo(Scalar(255,255,255));

            // ===== Actualizar y dibujar celdas =====
            for (size_t i = 0; i < celdas.size(); ++i) {
                Celda &c = celdas[i];
                // animación de tamaño
                if (c.creciendo) {
                    c.factorTamano = min(1.0, c.factorTamano + 0.05);
                    if (c.factorTamano >= 1.0) c.creciendo = false;
                } else if (c.encogiendo) {
                    c.factorTamano = max(0.0, c.factorTamano - 0.05);
                    if (c.factorTamano <= 0.0) c.encogiendo = false;
                }
                // reglas de vida
                if (--c.cuentaAtrasFotogramas <= 0 && !c.zonaSinGeneracion) {
                    int xIdx = i % columnas, yIdx = i / columnas, cnt = 0;
                    for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx)
                        if (dx||dy) {
                            int nx = (xIdx+dx+columnas)%columnas;
                            int ny = (yIdx+dy+filas)%filas;
                            if (celdas[ny*columnas + nx].viva) cnt++;
                        }
                    bool siguienteViva = c.viva ? (cnt==2||cnt==3) : (cnt==3);
                    if (siguienteViva && !c.viva) {
                        c.viva     = true;
                        c.factorTamano= 0;
                        c.creciendo   = true;
                        c.indiceColor  = colorActual;
                    } else if (!siguienteViva && c.viva) {
                        c.viva     = false;
                        c.factorTamano= 1;
                        c.encogiendo = true;
                    } else if (!c.viva && dist01(rng) < cfg.tasaEspontanea) {
                        c.viva     = true;
                        c.factorTamano= 0;
                        c.creciendo   = true;
                        c.indiceColor  = colorActual;
                    }
                    c.cuentaAtrasFotogramas = c.fotogramasPorActualizacion;
                }
                // renderizar celdas vivas
                if (c.factorTamano > 0) {
                    Scalar col = hex2Scalar(coloresHex[c.indiceColor]);
                    double s = tamanoCelda * c.factorTamano;
                    Point2f tl(c.centro.x - s/2, c.centro.y - s/2);
                    rectangle(fotograma,
                              Rect2f(tl.x, tl.y, (float)s, (float)s),
                              col, FILLED);
                }
            }

            // ===== Actualizar y dibujar ítems =====
            for (auto &itm : items) {
                if (itm.recolectado) continue;
                itm.vel.y += cfg.gravedad * dt;
                itm.pos   += itm.vel * dt;
                // colisión con celdas
                for (auto &c : celdas) {
                    if (c.factorTamano <= 0 || c.zonaSinGeneracion) continue;
                    double s = tamanoCelda * c.factorTamano;
                    Rect2f br(c.centro.x - s/2, c.centro.y - s/2,
                              (float)s, (float)s);
                    Rect2f ir(itm.pos.x-itm.radio,
                              itm.pos.y-itm.radio,
                              itm.radio*2, itm.radio*2);
                    if ((ir & br).area() > 0) {
                        itm.pos.y = br.y - itm.radio;
                        itm.vel.y = 0;
                    }
                }
                circle(fotograma, itm.pos, (int)itm.radio,
                       Scalar(0,215,255), FILLED);
                // recolección
                float dx = itm.pos.x - jugador.pos.x;
                float dy = itm.pos.y - jugador.pos.y;
                if (dx*dx + dy*dy < pow(itm.radio+jugador.radio,2)) {
                    itm.recolectado = true;
                    puntuacion += 10;
                }
            }

            // ===== Colisión del jugador y verificación de suelo =====
            jugador.enSuelo = false;
            Rect2f pr(jugador.pos.x-jugador.radio,
                      jugador.pos.y-jugador.radio,
                      jugador.radio*2, jugador.radio*2);
            for (auto &c : celdas) {
                if (c.factorTamano<=0 || c.zonaSinGeneracion) continue;
                double s = tamanoCelda*c.factorTamano;
                Rect2f br(c.centro.x - s/2, c.centro.y - s/2,
                          (float)s, (float)s);
                Rect2f inter = pr & br;
                if (inter.area()>0) {
                    if (inter.width < inter.height) {
                        jugador.pos.x += (pr.x+pr.width/2 < br.x+br.width/2)
                                      ? -inter.width : inter.width;
                        jugador.vel.x = 0;
                    } else {
                        jugador.pos.y += (pr.y+pr.height/2 < br.y+br.height/2)
                                      ? -inter.height : inter.height;
                        jugador.vel.y = 0;
                        if (pr.y+pr.height/2 < br.y+br.height/2)
                            jugador.enSuelo = true;
                    }
                    pr = Rect2f(jugador.pos.x-jugador.radio,
                                jugador.pos.y-jugador.radio,
                                jugador.radio*2, jugador.radio*2);
                }
            }

            // Salto
            if (salto && jugador.enSuelo) {
                jugador.vel.y = -cfg.velocidadSalto;
                jugador.enSuelo = false;
            }

            // Color del jugador y dibujo
            Scalar colorJugador;
            bool corriendo = fabs(jugador.vel.x)>0.1 && jugador.enSuelo;
            if (!jugador.enSuelo)
                colorJugador = jugador.mirandoDerecha
                            ? Scalar(0,255,255)
                            : Scalar(255,0,0);
            else if (corriendo)
                colorJugador = jugador.mirandoDerecha
                            ? ((cuentaGlobalFotogramas%2==0)
                                ? Scalar(0,255,0)
                                : Scalar(255,0,255))
                            : ((cuentaGlobalFotogramas%2==0)
                                ? Scalar(0,0,255)
                                : Scalar(255,255,0));
            else
                colorJugador = jugador.mirandoDerecha
                            ? Scalar(0,255,0)
                            : Scalar(0,0,255);

            circle(fotograma, jugador.pos, (int)jugador.radio,
                   colorJugador, FILLED);

            // Condiciones de derrota
            if (jugador.pos.x - jugador.radio <= 0
             || jugador.pos.y + jugador.radio >= alto) {
                salir = true; // reiniciar
            }

            // Superposición de nivel
            if (cuentaMostrarNivel-- > 0) {
                string niv = "Nivel " + to_string(nivel);
                int fuente = FONT_HERSHEY_SIMPLEX;
                double escala = 3.0;
                int grosor = 4;
                Size ts = getTextSize(niv,fuente,escala,grosor,nullptr);
                Point org((ancho-ts.width)/2, (alto+ts.height)/2);
                putText(fotograma, niv, org, fuente, escala,
                        Scalar(255,255,255), grosor+10);
                putText(fotograma, niv, org, fuente, escala,
                        Scalar(0,0,0), grosor);
            }

            // Superposición de título
            if (mostrarTitulo && cuentaMostrarTitulo-- > 0) {
                string t = "automata";
                int fuente = FONT_HERSHEY_SIMPLEX;
                double escala = 4.0;
                int grosor = 6;
                Size ts = getTextSize(t,fuente,escala,grosor,nullptr);
                Point org((ancho-ts.width)/2, (alto+ts.height)/2);
                putText(fotograma, t, org, fuente, escala,
                        Scalar(255,255,255), grosor+20);
                putText(fotograma, t, org, fuente, escala,
                        Scalar(0,0,0), grosor);
                if (cuentaMostrarTitulo == 0) mostrarTitulo = false;
            }

            // Superposición de puntuación
            putText(fotograma, "Puntos: " + to_string(puntuacion),
                    Point(10,30), FONT_HERSHEY_SIMPLEX,
                    1.0, Scalar(255,255,255), 10);
            putText(fotograma, "Puntuación: " + to_string(puntuacion),
                    Point(10,30), FONT_HERSHEY_SIMPLEX,
                    1.0, Scalar(0,0,0), 2);

            // Renderizar
            SDL_UpdateTexture(textura, NULL, fotograma.data, fotograma.step);
            SDL_RenderClear(renderizador);
            SDL_RenderCopy(renderizador, textura, NULL, NULL);
            SDL_RenderPresent(renderizador);

            cuentaGlobalFotogramas++;
            SDL_Delay(1000 / fps);
        }
    }

    // Limpieza
    SDL_DestroyTexture(textura);
    SDL_DestroyRenderer(renderizador);
    SDL_DestroyWindow(ventana);
    SDL_Quit();
    return 0;
}

