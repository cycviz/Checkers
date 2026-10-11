#pragma once
#include <tuple>

#include "../Models/Move.h"
#include "../Models/Response.h"
#include "Board.h"

// Класс Hand отвечает за ввод игрока: ловит события SDL (клики мыши, закрытие окна,
// изменение размера) и переводит их в Response и координаты клетки доски
class Hand
{
  public:
    // Hand хранит указатель на доску: нужны её размеры (W, H) и история ходов
    Hand(Board *board) : board(board)
    {
    }

    // Ожидание действия игрока во время хода.
    // Возвращает кортеж {ответ, строка, столбец}; координаты имеют смысл только при CELL
    tuple<Response, POS_T, POS_T> get_cell() const
    {
        SDL_Event windowEvent;
        Response resp = Response::OK;
        int x = -1, y = -1;   // координаты клика в пикселях
        int xc = -1, yc = -1; // координаты клика в клетках (строка, столбец)

        // Крутимся, пока не произойдёт значимое событие (resp станет не OK)
        while (true)
        {
            if (SDL_PollEvent(&windowEvent))
            {
                switch (windowEvent.type)
                {
                case SDL_QUIT: // закрыто окно
                    resp = Response::QUIT;
                    break;
                case SDL_MOUSEBUTTONDOWN: // клик мышью
                    x = windowEvent.motion.x;
                    y = windowEvent.motion.y;
                    // Окно делится на сетку 10x10: доска 8x8 занимает клетки 0..7,
                    // а по краям — рамка шириной в одну клетку (индексы -1 и 8).
                    // Переводим пиксели в номер строки (по y) и столбца (по x)
                    xc = int(y / (board->H / 10) - 1);
                    yc = int(x / (board->W / 10) - 1);
                    // Левый верхний угол рамки — кнопка «назад»;
                    // работает, только если в истории есть хотя бы один ход
                    if (xc == -1 && yc == -1 && board->history_mtx.size() > 1)
                    {
                        resp = Response::BACK;
                    }
                    // Правый верхний угол рамки — кнопка «заново»
                    else if (xc == -1 && yc == 8)
                    {
                        resp = Response::REPLAY;
                    }
                    // Клик внутри доски — выбрана клетка
                    else if (xc >= 0 && xc < 8 && yc >= 0 && yc < 8)
                    {
                        resp = Response::CELL;
                    }
                    // Клик по рамке вне кнопок — игнорируем
                    else
                    {
                        xc = -1;
                        yc = -1;
                    }
                    break;
                case SDL_WINDOWEVENT: // изменился размер окна — перерисовываем доску под новый размер
                    if (windowEvent.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    {
                        board->reset_window_size();
                        break;
                    }
                }
                if (resp != Response::OK)
                    break;
            }
        }
        return {resp, xc, yc};
    }

    // Ожидание действия на экране с результатом партии.
    // Реагирует только на закрытие окна (QUIT) и кнопку «заново» (REPLAY), остальные клики игнорируются
    Response wait() const
    {
        SDL_Event windowEvent;
        Response resp = Response::OK;
        while (true)
        {
            if (SDL_PollEvent(&windowEvent))
            {
                switch (windowEvent.type)
                {
                case SDL_QUIT: // закрыто окно
                    resp = Response::QUIT;
                    break;
                case SDL_WINDOWEVENT_SIZE_CHANGED: // изменение размера окна (см. примечание ниже)
                    board->reset_window_size();
                    break;
                case SDL_MOUSEBUTTONDOWN: { // клик: проверяем только кнопку «заново» в правом верхнем углу
                    int x = windowEvent.motion.x;
                    int y = windowEvent.motion.y;
                    int xc = int(y / (board->H / 10) - 1);
                    int yc = int(x / (board->W / 10) - 1);
                    if (xc == -1 && yc == 8)
                        resp = Response::REPLAY;
                }
                break;
                }
                if (resp != Response::OK)
                    break;
            }
        }
        return resp;
    }

  private:
    Board *board; // доска, с которой работает ввод
};