#pragma once
#include <iostream>
#include <fstream>
#include <vector>

#include "../Models/Move.h"
#include "../Models/Project_path.h"

// На macOS заголовки SDL ставятся в подпапку SDL2/, на Windows (NuGet) — напрямую
#ifdef __APPLE__
    #include <SDL2/SDL.h>
    #include <SDL2/SDL_image.h>
#else
    #include <SDL.h>
    #include <SDL_image.h>
#endif

using namespace std;

// Класс Board — игровая доска: хранит расположение шашек и историю ходов,
// выполняет ходы и отрисовывает всё в окне через SDL
class Board
{
public:
    Board() = default;
    // W, H — размер окна из settings.json (0 — подобрать автоматически)
    Board(const unsigned int W, const unsigned int H) : W(W), H(H)
    {
    }

    // Начальная отрисовка: инициализирует SDL, создаёт окно и рендерер,
    // загружает текстуры, расставляет шашки. Возвращает 0 при успехе, 1 при ошибке
    int start_draw()
    {
        if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
        {
            print_exception("SDL_Init can't init SDL2 lib");
            return 1;
        }
        // Размер не задан в настройках — делаем квадратное окно чуть меньше
        // меньшей стороны экрана (минус 1/15)
        if (W == 0 || H == 0)
        {
            SDL_DisplayMode dm;
            if (SDL_GetDesktopDisplayMode(0, &dm))
            {
                print_exception("SDL_GetDesktopDisplayMode can't get desctop display mode");
                return 1;
            }
            W = min(dm.w, dm.h);
            W -= W / 15;
            H = W;
        }
        // Окно с возможностью изменять размер; рендерер с аппаратным ускорением
        // и вертикальной синхронизацией
        win = SDL_CreateWindow("Checkers", 0, H / 30, W, H, SDL_WINDOW_RESIZABLE);
        if (win == nullptr)
        {
            print_exception("SDL_CreateWindow can't create window");
            return 1;
        }
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (ren == nullptr)
        {
            print_exception("SDL_CreateRenderer can't create renderer");
            return 1;
        }
        // Загружаем все текстуры из папки Textures
        board = IMG_LoadTexture(ren, board_path.c_str());
        w_piece = IMG_LoadTexture(ren, piece_white_path.c_str());
        b_piece = IMG_LoadTexture(ren, piece_black_path.c_str());
        w_queen = IMG_LoadTexture(ren, queen_white_path.c_str());
        b_queen = IMG_LoadTexture(ren, queen_black_path.c_str());
        back = IMG_LoadTexture(ren, back_path.c_str());
        replay = IMG_LoadTexture(ren, replay_path.c_str());
        if (!board || !w_piece || !b_piece || !w_queen || !b_queen || !back || !replay)
        {
            print_exception("IMG_LoadTexture can't load main textures from " + textures_path);
            return 1;
        }
        // Фактический размер области рисования в пикселях (на Retina он больше размера окна)
        SDL_GetRendererOutputSize(ren, &W, &H);
        make_start_mtx();
        rerender();
        return 0;
    }

    // Сброс доски для новой партии: начальная расстановка, пустая история,
    // без выделения и без экрана результата
    void redraw()
    {
        game_results = -1;
        history_mtx.clear();
        history_beat_series.clear();
        make_start_mtx();
        clear_active();
        clear_highlight();
    }

    // Выполнить ход turn: если это взятие — убрать побитую шашку, затем переместить свою.
    // beat_series — номер взятия в текущей серии (0 — ход без взятия), сохраняется в историю для отката
    void move_piece(move_pos turn, const int beat_series = 0)
    {
        if (turn.xb != -1)
        {
            mtx[turn.xb][turn.yb] = 0;
        }
        move_piece(turn.x, turn.y, turn.x2, turn.y2, beat_series);
    }

    // Переместить шашку с (i, j) на (i2, j2)
    void move_piece(const POS_T i, const POS_T j, const POS_T i2, const POS_T j2, const int beat_series = 0)
    {
        // Защита от некорректного хода: конечная клетка должна быть пустой, а начальная — занятой
        if (mtx[i2][j2])
        {
            throw runtime_error("final position is not empty, can't move");
        }
        if (!mtx[i][j])
        {
            throw runtime_error("begin position is empty, can't move");
        }
        // Превращение в дамку: белая шашка дошла до верхнего ряда (0), чёрная — до нижнего (7)
        if ((mtx[i][j] == 1 && i2 == 0) || (mtx[i][j] == 2 && i2 == 7))
            mtx[i][j] += 2;
        mtx[i2][j2] = mtx[i][j];
        drop_piece(i, j);
        // Сохраняем новую позицию в историю
        add_history(beat_series);
    }

    // Убрать шашку с клетки (i, j) и перерисовать доску
    void drop_piece(const POS_T i, const POS_T j)
    {
        mtx[i][j] = 0;
        rerender();
    }

    // Превратить шашку на (i, j) в дамку (1 -> 3, 2 -> 4)
    void turn_into_queen(const POS_T i, const POS_T j)
    {
        if (mtx[i][j] == 0 || mtx[i][j] > 2)
        {
            throw runtime_error("can't turn into queen in this position");
        }
        mtx[i][j] += 2;
        rerender();
    }
    // Копия текущей матрицы доски (используется ботом для перебора ходов)
    vector<vector<POS_T>> get_board() const
    {
        return mtx;
    }

    // Подсветить клетки (зелёная рамка) — например, шашки, которыми можно ходить
    void highlight_cells(vector<pair<POS_T, POS_T>> cells)
    {
        for (auto pos : cells)
        {
            POS_T x = pos.first, y = pos.second;
            is_highlighted_[x][y] = 1;
        }
        rerender();
    }

    // Снять подсветку со всех клеток
    void clear_highlight()
    {
        for (POS_T i = 0; i < 8; ++i)
        {
            is_highlighted_[i].assign(8, 0);
        }
        rerender();
    }

    // Отметить выбранную шашку (красная рамка)
    void set_active(const POS_T x, const POS_T y)
    {
        active_x = x;
        active_y = y;
        rerender();
    }

    // Снять выбор шашки
    void clear_active()
    {
        active_x = -1;
        active_y = -1;
        rerender();
    }

    // Подсвечена ли клетка (x, y)
    bool is_highlighted(const POS_T x, const POS_T y)
    {
        return is_highlighted_[x][y];
    }

    // Откат одного хода. Ход с серией взятий хранится в истории несколькими записями
    // (по одной на каждое взятие), поэтому снимаем столько записей, сколько взятий
    // было в последней серии (минимум одну). Начальную позицию не удаляем никогда
    void rollback()
    {
        auto beat_series = max(1, *(history_beat_series.rbegin()));
        while (beat_series-- && history_mtx.size() > 1)
        {
            history_mtx.pop_back();
            history_beat_series.pop_back();
        }
        // Текущей становится последняя оставшаяся позиция
        mtx = *(history_mtx.rbegin());
        clear_highlight();
        clear_active();
    }

    // Показать результат партии: 0 — ничья, 1 — победа белых, 2 — победа чёрных
    void show_final(const int res)
    {
        game_results = res;
        rerender();
    }

    // Вызывается при изменении размера окна: обновляем W, H и перерисовываем
    void reset_window_size()
    {
        SDL_GetRendererOutputSize(ren, &W, &H);
        rerender();
    }

    // Освобождение ресурсов SDL: текстуры, рендерер, окно, сама библиотека
    void quit()
    {
        SDL_DestroyTexture(board);
        SDL_DestroyTexture(w_piece);
        SDL_DestroyTexture(b_piece);
        SDL_DestroyTexture(w_queen);
        SDL_DestroyTexture(b_queen);
        SDL_DestroyTexture(back);
        SDL_DestroyTexture(replay);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
    }

    // Деструктор: освобождаем SDL, только если окно было создано
    ~Board()
    {
        if (win)
            quit();
    }

private:
    // Добавить текущую позицию в историю вместе с номером взятия в серии
    void add_history(const int beat_series = 0)
    {
        history_mtx.push_back(mtx);
        history_beat_series.push_back(beat_series);
    }
    // Начальная расстановка: шашки стоят на тёмных клетках ((i + j) нечётно),
    // чёрные (2) — в рядах 0..2 сверху, белые (1) — в рядах 5..7 снизу.
    // Начальная позиция становится первой записью истории
    void make_start_mtx()
    {
        for (POS_T i = 0; i < 8; ++i)
        {
            for (POS_T j = 0; j < 8; ++j)
            {
                mtx[i][j] = 0;
                if (i < 3 && (i + j) % 2 == 1)
                    mtx[i][j] = 2;
                if (i > 4 && (i + j) % 2 == 1)
                    mtx[i][j] = 1;
            }
        }
        add_history();
    }

    // Полная перерисовка кадра: доска, шашки, подсветка, выбранная шашка, кнопки, результат.
    // Окно делится на сетку 10x10: доска занимает клетки 1..8, по краям — рамка в одну клетку
    void rerender()
    {
        // Фон — картинка доски на всё окно
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, board, NULL, NULL);

        // Шашки: для каждой непустой клетки выбираем текстуру по типу фигуры
        // и рисуем её с небольшим отступом от края клетки
        for (POS_T i = 0; i < 8; ++i)
        {
            for (POS_T j = 0; j < 8; ++j)
            {
                if (!mtx[i][j])
                    continue;
                int wpos = W * (j + 1) / 10 + W / 120;
                int hpos = H * (i + 1) / 10 + H / 120;
                SDL_Rect rect{ wpos, hpos, W / 12, H / 12 };

                SDL_Texture* piece_texture;
                if (mtx[i][j] == 1)
                    piece_texture = w_piece;
                else if (mtx[i][j] == 2)
                    piece_texture = b_piece;
                else if (mtx[i][j] == 3)
                    piece_texture = w_queen;
                else
                    piece_texture = b_queen;

                SDL_RenderCopy(ren, piece_texture, NULL, &rect);
            }
        }

        // Подсвеченные клетки — зелёные рамки. Масштаб 2.5 делает линии рамки толще,
        // поэтому координаты делятся на scale
        SDL_SetRenderDrawColor(ren, 0, 255, 0, 0);
        const double scale = 2.5;
        SDL_RenderSetScale(ren, scale, scale);
        for (POS_T i = 0; i < 8; ++i)
        {
            for (POS_T j = 0; j < 8; ++j)
            {
                if (!is_highlighted_[i][j])
                    continue;
                SDL_Rect cell{ int(W * (j + 1) / 10 / scale), int(H * (i + 1) / 10 / scale), int(W / 10 / scale),
                              int(H / 10 / scale) };
                SDL_RenderDrawRect(ren, &cell);
            }
        }

        // Выбранная шашка — красная рамка
        if (active_x != -1)
        {
            SDL_SetRenderDrawColor(ren, 255, 0, 0, 0);
            SDL_Rect active_cell{ int(W * (active_y + 1) / 10 / scale), int(H * (active_x + 1) / 10 / scale),
                                 int(W / 10 / scale), int(H / 10 / scale) };
            SDL_RenderDrawRect(ren, &active_cell);
        }
        SDL_RenderSetScale(ren, 1, 1);

        // Кнопки «назад» (левый верхний угол) и «заново» (правый верхний угол)
        SDL_Rect rect_left{ W / 40, H / 40, W / 15, H / 15 };
        SDL_RenderCopy(ren, back, NULL, &rect_left);
        SDL_Rect replay_rect{ W * 109 / 120, H / 40, W / 15, H / 15 };
        SDL_RenderCopy(ren, replay, NULL, &replay_rect);

        // Если партия окончена — картинка с результатом поверх доски
        if (game_results != -1)
        {
            string result_path = draw_path;
            if (game_results == 1)
                result_path = white_path;
            else if (game_results == 2)
                result_path = black_path;
            SDL_Texture* result_texture = IMG_LoadTexture(ren, result_path.c_str());
            if (result_texture == nullptr)
            {
                print_exception("IMG_LoadTexture can't load game result picture from " + result_path);
                return;
            }
            SDL_Rect res_rect{ W / 5, H * 3 / 10, W * 3 / 5, H * 2 / 5 };
            SDL_RenderCopy(ren, result_texture, NULL, &res_rect);
            SDL_DestroyTexture(result_texture);
        }

        // Выводим готовый кадр на экран
        SDL_RenderPresent(ren);
        // На macOS окно обновляется только при обработке событий,
        // поэтому делаем короткую паузу и забираем одно событие из очереди
        SDL_Delay(10);
        SDL_Event windowEvent;
        SDL_PollEvent(&windowEvent);
    }

    // Запись ошибки в log.txt вместе с описанием ошибки от SDL
    void print_exception(const string& text) {
        ofstream fout(project_path + "log.txt", ios_base::app);
        fout << "Error: " << text << ". "<< SDL_GetError() << endl;
        fout.close();
    }

  public:
    int W = 0; // ширина области рисования в пикселях
    int H = 0; // высота области рисования в пикселях
    // История позиций: после каждого хода (и каждого взятия в серии) сюда добавляется копия доски
    vector<vector<vector<POS_T>>> history_mtx;

  private:
    SDL_Window *win = nullptr;    // окно игры
    SDL_Renderer *ren = nullptr;  // рендерер, через который идёт вся отрисовка
    // Текстуры
    SDL_Texture *board = nullptr;   // доска
    SDL_Texture *w_piece = nullptr; // белая шашка
    SDL_Texture *b_piece = nullptr; // чёрная шашка
    SDL_Texture *w_queen = nullptr; // белая дамка
    SDL_Texture *b_queen = nullptr; // чёрная дамка
    SDL_Texture *back = nullptr;    // кнопка «назад»
    SDL_Texture *replay = nullptr;  // кнопка «заново»
    // Пути к файлам текстур
    const string textures_path = project_path + "Textures/";
    const string board_path = textures_path + "board.png";
    const string piece_white_path = textures_path + "piece_white.png";
    const string piece_black_path = textures_path + "piece_black.png";
    const string queen_white_path = textures_path + "queen_white.png";
    const string queen_black_path = textures_path + "queen_black.png";
    const string white_path = textures_path + "white_wins.png";
    const string black_path = textures_path + "black_wins.png";
    const string draw_path = textures_path + "draw.png";
    const string back_path = textures_path + "back.png";
    const string replay_path = textures_path + "replay.png";
    // Координаты выбранной шашки; -1 — ничего не выбрано
    int active_x = -1, active_y = -1;
    // Результат партии: -1 — партия идёт, 0 — ничья, 1 — победа белых, 2 — победа чёрных
    int game_results = -1;
    // Матрица подсветки: true — клетка подсвечена (сюда можно сходить / этой шашкой можно ходить)
    vector<vector<bool>> is_highlighted_ = vector<vector<bool>>(8, vector<bool>(8, 0));
    // Матрица доски 8x8: 0 — пусто, 1 — белая шашка, 2 — чёрная, 3 — белая дамка, 4 — чёрная дамка
    vector<vector<POS_T>> mtx = vector<vector<POS_T>>(8, vector<POS_T>(8, 0));
    // Для каждой записи history_mtx — номер взятия в серии (0 — ход без взятия); нужно для rollback()
    vector<int> history_beat_series;
};