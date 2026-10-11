#pragma once
#include <random>
#include <vector>

#include "../Models/Move.h"
#include "Board.h"
#include "Config.h"

// «Бесконечность» для оценки позиции: так оценивается выигранная позиция
const int INF = 1e9;

// Класс Logic — игровая логика: поиск допустимых ходов по правилам шашек
// и выбор лучшего хода для бота
class Logic
{
  public:
    // Читаем настройки бота один раз при создании объекта:
    // NoRandom — фиксированный seed генератора (0) или seed от текущего времени,
    // BotScoringType — способ оценки позиции, Optimization — включено ли альфа-бета отсечение
    Logic(Board *board, Config *config) : board(board), config(config)
    {
        rand_eng = std::default_random_engine (
            !((*config)("Bot", "NoRandom")) ? unsigned(time(0)) : 0);
        scoring_mode = (*config)("Bot", "BotScoringType");
        optimization = (*config)("Bot", "Optimization");
    }

    vector<move_pos> find_best_turns(const bool color)
    {
        return {};
    }

private:
    // Применить ход turn к копии доски mtx и вернуть получившуюся позицию.
    // Реальная доска не меняется — функция нужна боту, чтобы «примерять» ходы при переборе
    vector<vector<POS_T>> make_turn(vector<vector<POS_T>> mtx, move_pos turn) const
    {
        // Если ход со взятием — убираем побитую шашку
        if (turn.xb != -1)
            mtx[turn.xb][turn.yb] = 0;
        // Превращение в дамку: белая шашка дошла до ряда 0, чёрная — до ряда 7
        if ((mtx[turn.x][turn.y] == 1 && turn.x2 == 0) || (mtx[turn.x][turn.y] == 2 && turn.x2 == 7))
            mtx[turn.x][turn.y] += 2;
        // Переносим фигуру на новую клетку и освобождаем старую
        mtx[turn.x2][turn.y2] = mtx[turn.x][turn.y];
        mtx[turn.x][turn.y] = 0;
        return mtx;
    }

    // Оценка позиции mtx с точки зрения бота: отношение «материала» бота к «материалу» соперника.
    // Чем больше значение, тем позиция лучше для бота.
    // first_bot_color — цвет бота, для которого считается оценка (0 — белые, 1 — чёрные)
    double calc_score(const vector<vector<POS_T>> &mtx, const bool first_bot_color) const
    {
        // Считаем фигуры: w — белые шашки, wq — белые дамки, b — чёрные шашки, bq — чёрные дамки
        double w = 0, wq = 0, b = 0, bq = 0;
        for (POS_T i = 0; i < 8; ++i)
        {
            for (POS_T j = 0; j < 8; ++j)
            {
                w += (mtx[i][j] == 1);
                wq += (mtx[i][j] == 3);
                b += (mtx[i][j] == 2);
                bq += (mtx[i][j] == 4);
                // В режиме NumberAndPotential шашка стоит дороже, чем ближе она к превращению в дамку:
                // +0.05 за каждый пройденный ряд (белые идут к ряду 0, чёрные — к ряду 7)
                if (scoring_mode == "NumberAndPotential")
                {
                    w += 0.05 * (mtx[i][j] == 1) * (7 - i);
                    b += 0.05 * (mtx[i][j] == 2) * (i);
                }
            }
        }
        // Формула ниже считает в пользу чёрных. Если бот играет белыми —
        // меняем стороны местами: теперь b, bq — фигуры бота, w, wq — фигуры соперника
        if (!first_bot_color)
        {
            swap(b, w);
            swap(bq, wq);
        }
        // У соперника не осталось фигур — бот выиграл, максимальная оценка
        if (w + wq == 0)
            return INF;
        // У бота не осталось фигур — проигрыш, минимальная оценка
        if (b + bq == 0)
            return 0;
        // Дамка ценится как несколько шашек: 4 в простом режиме, 5 — в NumberAndPotential
        int q_coef = 4;
        if (scoring_mode == "NumberAndPotential")
        {
            q_coef = 5;
        }
        // Итоговая оценка — отношение материала бота к материалу соперника
        return (b + bq * q_coef) / (w + wq * q_coef);
    }

    double find_first_best_turn(vector<vector<POS_T>> mtx, const bool color, const POS_T x, const POS_T y, size_t state,
                                double alpha = -1)
    {
        return 0;
    }

    double find_best_turns_rec(vector<vector<POS_T>> mtx, const bool color, const size_t depth, double alpha = -1,
                               double beta = INF + 1, const POS_T x = -1, const POS_T y = -1)
    {
        return 0;
    }

public:
    // Публичные версии find_turns работают с текущей позицией на доске (board->get_board()).
    // Результат записывается в поля turns и have_beats.

    // Найти все допустимые ходы для цвета color (0 — белые, 1 — чёрные)
    void find_turns(const bool color)
    {
        find_turns(color, board->get_board());
    }

    // Найти все допустимые ходы для фигуры на клетке (x, y)
    // (используется для продолжения серии взятий)
    void find_turns(const POS_T x, const POS_T y)
    {
        find_turns(x, y, board->get_board());
    }

private:
    // Найти все ходы цвета color в произвольной позиции mtx (бот вызывает её для «примерочных» позиций).
    // Учитывает правило обязательного взятия: если хоть одна фигура может бить,
    // в результат попадают только ходы со взятием
    void find_turns(const bool color, const vector<vector<POS_T>> &mtx)
    {
        vector<move_pos> res_turns;
        bool have_beats_before = false; // нашлось ли уже хоть одно взятие
        for (POS_T i = 0; i < 8; ++i)
        {
            for (POS_T j = 0; j < 8; ++j)
            {
                // Фигура нужного цвета: у белых коды 1 и 3 (нечётные), у чёрных — 2 и 4 (чётные)
                if (mtx[i][j] && mtx[i][j] % 2 != color)
                {
                    // Ходы этой фигуры
                    find_turns(i, j, mtx);
                    // Первое найденное взятие: выбрасываем все собранные ранее обычные ходы
                    if (have_beats && !have_beats_before)
                    {
                        have_beats_before = true;
                        res_turns.clear();
                    }
                    // Добавляем ходы фигуры: если взятия уже есть — только её взятия,
                    // если взятий пока нет — любые её ходы
                    if ((have_beats_before && have_beats) || !have_beats_before)
                    {
                        res_turns.insert(res_turns.end(), turns.begin(), turns.end());
                    }
                }
            }
        }
        turns = res_turns;
        // Перемешиваем ходы, чтобы среди равноценных бот выбирал случайный, а не всегда первый
        shuffle(turns.begin(), turns.end(), rand_eng);
        have_beats = have_beats_before;
    }

    // Найти все ходы фигуры на клетке (x, y) в позиции mtx.
    // Сначала ищутся взятия; если они есть — возвращаются только они (взятие обязательно),
    // иначе ищутся обычные ходы
    void find_turns(const POS_T x, const POS_T y, const vector<vector<POS_T>> &mtx)
    {
        turns.clear();
        have_beats = false;
        POS_T type = mtx[x][y]; // тип фигуры: 1, 2 — шашки, 3, 4 — дамки
        // Поиск взятий
        switch (type)
        {
        case 1:
        case 2:
            // Шашка бьёт в любую из 4 диагональных сторон (в том числе назад):
            // перепрыгивает через соседнюю клетку на клетку через одну
            for (POS_T i = x - 2; i <= x + 2; i += 4)
            {
                for (POS_T j = y - 2; j <= y + 2; j += 4)
                {
                    // Клетка приземления за пределами доски
                    if (i < 0 || i > 7 || j < 0 || j > 7)
                        continue;
                    // (xb, yb) — клетка между начальной и конечной, где должна стоять побитая фигура
                    POS_T xb = (x + i) / 2, yb = (y + j) / 2;
                    // Взятие невозможно, если клетка приземления занята,
                    // в середине пусто или там стоит своя фигура
                    if (mtx[i][j] || !mtx[xb][yb] || mtx[xb][yb] % 2 == type % 2)
                        continue;
                    turns.emplace_back(x, y, i, j, xb, yb);
                }
            }
            break;
        default:
            // Дамка бьёт на любом расстоянии: идём по каждой из 4 диагоналей,
            // ищем первую фигуру соперника, за ней — пустые клетки для приземления
            for (POS_T i = -1; i <= 1; i += 2)
            {
                for (POS_T j = -1; j <= 1; j += 2)
                {
                    POS_T xb = -1, yb = -1; // найденная на диагонали фигура соперника
                    for (POS_T i2 = x + i, j2 = y + j; i2 != 8 && j2 != 8 && i2 != -1 && j2 != -1; i2 += i, j2 += j)
                    {
                        if (mtx[i2][j2])
                        {
                            // Своя фигура или вторая фигура соперника подряд — дальше по диагонали бить нельзя
                            if (mtx[i2][j2] % 2 == type % 2 || (mtx[i2][j2] % 2 != type % 2 && xb != -1))
                            {
                                break;
                            }
                            // Первая фигура соперника на пути — кандидат на взятие
                            xb = i2;
                            yb = j2;
                        }
                        // Любая пустая клетка за побитой фигурой — допустимое место приземления
                        if (xb != -1 && xb != i2)
                        {
                            turns.emplace_back(x, y, i2, j2, xb, yb);
                        }
                    }
                }
            }
            break;
        }
        // Если взятия есть — обычные ходы не ищем
        if (!turns.empty())
        {
            have_beats = true;
            return;
        }
        // Поиск обычных ходов (без взятия)
        switch (type)
        {
        case 1:
        case 2:
            // Шашка ходит на одну клетку по диагонали только вперёд:
            // белые (1) — вверх (x - 1), чёрные (2) — вниз (x + 1)
            {
                POS_T i = ((type % 2) ? x - 1 : x + 1);
                for (POS_T j = y - 1; j <= y + 1; j += 2)
                {
                    // Пропускаем клетки за пределами доски и занятые
                    if (i < 0 || i > 7 || j < 0 || j > 7 || mtx[i][j])
                        continue;
                    turns.emplace_back(x, y, i, j);
                }
                break;
            }
        default:
            // Дамка ходит на любое число клеток по каждой диагонали, пока не упрётся в фигуру или край доски
            for (POS_T i = -1; i <= 1; i += 2)
            {
                for (POS_T j = -1; j <= 1; j += 2)
                {
                    for (POS_T i2 = x + i, j2 = y + j; i2 != 8 && j2 != 8 && i2 != -1 && j2 != -1; i2 += i, j2 += j)
                    {
                        if (mtx[i2][j2])
                            break;
                        turns.emplace_back(x, y, i2, j2);
                    }
                }
            }
            break;
        }
    }

  public:
    vector<move_pos> turns; // ходы, найденные последним вызовом find_turns()
    bool have_beats;        // true — в turns только ходы со взятием (взятие обязательно)
    int Max_depth;          // глубина перебора бота (уровень из настроек); задаётся в Game::play() перед каждым ходом

  private:
    default_random_engine rand_eng; // генератор случайных чисел для перемешивания ходов
    string scoring_mode;            // способ оценки позиции: "Number" или "NumberAndPotential"
    string optimization;            // "O0" — полный перебор, иначе — с альфа-бета отсечением
    // Цепочка лучших ходов бота, построенная поиском: элементы с индексом state — «состояния»
    // (позиции в серии взятий). next_move[state] — лучший ход из этого состояния,
    // next_best_state[state] — индекс следующего состояния серии (-1 — серия закончилась)
    vector<move_pos> next_move;
    vector<int> next_best_state;
    Board *board;   // игровая доска (откуда берётся текущая позиция)
    Config *config; // настройки игры
};