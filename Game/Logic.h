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

    // Поиск лучшего хода бота за цвет color (0 — белые, 1 — чёрные).
    // Возвращает последовательность перемещений, которую бот должен выполнить:
    // одно перемещение для обычного хода или несколько — для серии взятий.
    vector<move_pos> find_best_turns(const bool color)
    {
        // Очищаем результаты прошлого поиска
        next_move.clear();
        next_best_state.clear();

        // Запускаем перебор из текущей позиции на доске. Корневое состояние имеет номер 0.
        // После поиска в next_move / next_best_state записана цепочка лучших ходов
        find_first_best_turn(board->get_board(), color, -1, -1, 0);

        // Восстанавливаем цепочку: начинаем с состояния 0 и идём по ссылкам next_best_state.
        // Цепочка заканчивается, когда ссылка равна -1 (серия взятий завершена)
        // или когда в состоянии не записан ход (x == -1 — продолжать серию нечем)
        vector<move_pos> res;
        int state = 0;
        while (state != -1 && next_move[state].x != -1)
        {
            res.push_back(next_move[state]);
            state = next_best_state[state];
        }
        return res;
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

    // Выбор первого хода бота (корень дерева поиска).
    // В отличие от find_best_turns_rec, функция не только оценивает ходы, но и запоминает,
    // какой ход лучший, — в next_move[state]. Если ход — взятие, после которого можно бить
    // дальше, бот обязан продолжить серию, поэтому каждое следующее взятие серии
    // выбирается так же, отдельным «состоянием» (state), а ссылка на него пишется в next_best_state[state].
    // mtx — позиция; color — цвет бота; (x, y) — шашка, продолжающая серию взятий (-1 — начало хода);
    // state — номер текущего состояния; alpha — оценка, которую бот уже гарантировал себе выше по дереву.
    // Возвращает оценку лучшего хода (чем больше, тем лучше для бота)
    double find_first_best_turn(vector<vector<POS_T>> mtx, const bool color, const POS_T x, const POS_T y, size_t state,
                                double alpha = -1)
    {
        // Заводим ячейки для текущего состояния: пока «хода нет» и «продолжения нет»
        next_move.emplace_back(-1, -1, -1, -1);
        next_best_state.push_back(-1);

        // Находим возможные ходы: в начале хода — для всех фигур цвета бота,
        // в середине серии — только для бьющей шашки
        if (state == 0)
            find_turns(color, mtx);
        else
            find_turns(x, y, mtx);
        // Копируем результат в локальные переменные: рекурсивные вызовы перезапишут поля turns и have_beats
        vector<move_pos> cur_turns = turns;
        bool cur_have_beats = have_beats;

        // Середина серии, но бить больше нечего — ход бота закончен.
        // Дальше ходит соперник: оцениваем позицию обычным минимаксом с глубины 0
        if (state != 0 && !cur_have_beats)
            return find_best_turns_rec(mtx, !color, 0, alpha);

        double best_score = -1; // оценка лучшего найденного хода; -1 — ниже любой реальной оценки
        for (const move_pos &turn : cur_turns)
        {
            vector<vector<POS_T>> new_mtx = make_turn(mtx, turn);
            double score;
            // Номер, который получит следующее состояние, если серия взятий продолжится
            size_t next_state = next_move.size();
            if (cur_have_beats)
            {
                // Взятие: ход бота может продолжиться той же шашкой — выбираем продолжение рекурсивно.
                // Передаём лучшую уже известную оценку, чтобы глубже отсекать заведомо худшие варианты
                score = find_first_best_turn(new_mtx, color, turn.x2, turn.y2, next_state, max(alpha, best_score));
            }
            else
            {
                // Обычный ход: ход бота закончен, отвечает соперник (глубина 0 — его ход)
                score = find_best_turns_rec(new_mtx, !color, 0, max(alpha, best_score));
            }
            // Нашли ход лучше — запоминаем его и (для взятия) ссылку на продолжение серии
            if (score > best_score)
            {
                best_score = score;
                next_move[state] = turn;
                next_best_state[state] = cur_have_beats ? int(next_state) : -1;
            }
        }
        return best_score;
    }

    // Минимакс с альфа-бета отсечением.
    // Оценивает позицию mtx, просматривая ходы обеих сторон на Max_depth ходов вперёд.
    // Чётная глубина (0, 2, ...) — ходит соперник бота: он выбирает ход с МИНИМАЛЬНОЙ оценкой.
    // Нечётная глубина (1, 3, ...) — ходит бот: он выбирает ход с МАКСИМАЛЬНОЙ оценкой.
    // color — цвет стороны, которая сейчас ходит; depth — текущая глубина;
    // alpha — лучшая оценка, которую бот уже гарантировал себе выше по дереву;
    // beta — лучшая (наименьшая) оценка, которую уже гарантировал себе соперник;
    // (x, y) — шашка, продолжающая серию взятий (-1 — начало хода).
    double find_best_turns_rec(vector<vector<POS_T>> mtx, const bool color, const size_t depth, double alpha = -1,
                               double beta = INF + 1, const POS_T x = -1, const POS_T y = -1)
    {
        // Достигли предельной глубины — дальше не перебираем, возвращаем оценку позиции для бота.
        // На чётной глубине ходит соперник, значит бот — другой цвет; на нечётной ходит сам бот
        if (depth == Max_depth)
        {
            bool bot_color = (depth % 2 == 0) ? !color : color;
            return calc_score(mtx, bot_color);
        }

        bool is_bot_turn = (depth % 2 == 1); // бот максимизирует, соперник минимизирует

        // Находим ходы: в середине серии — только для бьющей шашки, иначе — для всех фигур стороны
        if (x != -1)
            find_turns(x, y, mtx);
        else
            find_turns(color, mtx);
        vector<move_pos> cur_turns = turns;
        bool cur_have_beats = have_beats;

        // Середина серии, но бить больше нечего — ход переходит к другой стороне на следующей глубине
        if (x != -1 && !cur_have_beats)
            return find_best_turns_rec(mtx, !color, depth + 1, alpha, beta);

        // У стороны нет ходов — она проиграла.
        // Если это бот — худшая оценка (0), если соперник — лучшая для бота (INF)
        if (cur_turns.empty())
            return is_bot_turn ? 0 : INF;

        // Лучшая оценка на этом уровне: для бота ищем максимум, для соперника — минимум
        double best = is_bot_turn ? -1 : INF + 1;
        for (const move_pos &turn : cur_turns)
        {
            vector<vector<POS_T>> new_mtx = make_turn(mtx, turn);
            double score;
            if (cur_have_beats)
                // Взятие: та же сторона может продолжить серию той же шашкой, глубина не растёт —
                // вся серия взятий считается одним ходом
                score = find_best_turns_rec(new_mtx, color, depth, alpha, beta, turn.x2, turn.y2);
            else
                // Обычный ход: ход переходит к сопернику, глубина + 1
                score = find_best_turns_rec(new_mtx, !color, depth + 1, alpha, beta);

            // Обновляем лучшую оценку и соответствующую границу окна [alpha, beta]
            if (is_bot_turn)
            {
                best = max(best, score);
                alpha = max(alpha, best);
            }
            else
            {
                best = min(best, score);
                beta = min(beta, best);
            }

            // Альфа-бета отсечение (если не выключено настройкой Optimization = "O0").
            // alpha >= beta означает, что ни одна из сторон не допустит попадания в эту позицию:
            // противник выше по дереву уже имеет вариант не хуже, поэтому оставшиеся ходы
            // можно не перебирать — на итоговый выбор они не повлияют
            if (optimization != "O0" && alpha >= beta)
                break;
        }
        return best;
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