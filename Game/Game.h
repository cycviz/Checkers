#pragma once
#include <chrono>
#include <thread>

#include "../Models/Project_path.h"
#include "Board.h"
#include "Config.h"
#include "Hand.h"
#include "Logic.h"

class Game
{
  public:
    Game() : board(config("WindowSize", "Width"), config("WindowSize", "Hight")), hand(&board), logic(&board, &config)
    {
        ofstream fout(project_path + "log.txt", ios_base::trunc);
        fout.close();
    }

    // to start checkers
    // Запуск партии. Возвращает результат: 0 — ничья (или выход из игры), 1 — победа белых, 2 — победа чёрных
    int play()
    {
        // Засекаем время начала партии, чтобы в конце записать длительность в log.txt
        auto start = chrono::steady_clock::now();

        // Подготовка доски. Если это повторная партия (нажата кнопка replay):
        // пересоздаём логику бота, перечитываем settings.json и перерисовываем доску в начальной позиции.
        // Примечание: Logic создаётся до reload(), поэтому NoRandom, BotScoringType и Optimization
        // из обновлённого файла применятся только со следующей партии.
        if (is_replay)
        {
            logic = Logic(&board, &config);
            config.reload();
            board.redraw();
        }
        else
        {
            // Первая партия: создаём окно, загружаем текстуры, расставляем шашки
            board.start_draw();
        }
        is_replay = false;

        int turn_num = -1;      // номер текущего хода; чётный — ходят белые, нечётный — чёрные
        bool is_quit = false;   // флаг выхода из игры (закрыто окно)
        const int Max_turns = config("Game", "MaxNumTurns"); // лимит ходов, после которого — ничья

        // Главный игровой цикл: один проход = один ход одной стороны
        while (++turn_num < Max_turns)
        {
            beat_series = 0; // счётчик взятий подряд в текущем ходе (серия рубки)

            // Ищем все допустимые ходы для стороны turn_num % 2 (0 — белые, 1 — чёрные).
            // Нет ходов — эта сторона проиграла, выходим из цикла
            logic.find_turns(turn_num % 2);
            if (logic.turns.empty())
                break;

            // Глубина поиска бота берётся из настроек для цвета, который сейчас ходит
            logic.Max_depth = config("Bot", string((turn_num % 2) ? "Black" : "White") + string("BotLevel"));

            // Если текущей стороной играет человек — ждём его хода
            if (!config("Bot", string("Is") + string((turn_num % 2) ? "Black" : "White") + string("Bot")))
            {
                auto resp = player_turn(turn_num % 2);
                if (resp == Response::QUIT)        // окно закрыто — завершаем игру
                {
                    is_quit = true;
                    break;
                }
                else if (resp == Response::REPLAY) // нажата кнопка «заново» — начинаем новую партию
                {
                    is_replay = true;
                    break;
                }
                else if (resp == Response::BACK)   // нажата кнопка «назад» — откат хода
                {
                    // Если соперник — бот и мы не в середине серии взятий, откатываем
                    // ещё и ход бота, чтобы вернуть ход человеку
                    if (config("Bot", string("Is") + string((1 - turn_num % 2) ? "Black" : "White") + string("Bot")) &&
                        !beat_series && board.history_mtx.size() > 2)
                    {
                        board.rollback();
                        --turn_num;
                    }
                    if (!beat_series)
                        --turn_num;

                    // Откатываем предыдущий ход (вместе со всей его серией взятий).
                    // Уменьшаем turn_num с учётом ++ в начале цикла: ход вернётся нужной стороне
                    board.rollback();
                    --turn_num;
                    beat_series = 0;
                }
            }
            else
                // Текущей стороной играет бот — он сам выбирает и делает ход
                bot_turn(turn_num % 2);
        }

        // Партия окончена: записываем её длительность в лог
        auto end = chrono::steady_clock::now();
        ofstream fout(project_path + "log.txt", ios_base::app);
        fout << "Game time: " << (int)chrono::duration<double, milli>(end - start).count() << " millisec\n";
        fout.close();

        // Повтор партии — рекурсивно запускаем play() заново
        if (is_replay)
            return play();
        // Выход из игры без показа результата
        if (is_quit)
            return 0;

        // Определяем результат: по умолчанию победили чёрные (2).
        // Достигнут лимит ходов — ничья (0).
        // Иначе цикл прервался, потому что у ходящей стороны нет ходов:
        // если ходить должны были чёрные (нечётный ход) — победили белые (1)
        int res = 2;
        if (turn_num == Max_turns)
        {
            res = 0;
        }
        else if (turn_num % 2)
        {
            res = 1;
        }

        // Показываем экран с результатом и ждём действия игрока: replay — новая партия, иначе — выход
        board.show_final(res);
        auto resp = hand.wait();
        if (resp == Response::REPLAY)
        {
            is_replay = true;
            return play();
        }
        return res;
    }

  private:
    // Ход бота за цвет color (0 — белые, 1 — чёрные)
    void bot_turn(const bool color)
    {
        // Засекаем время, чтобы записать в лог, сколько бот думал над ходом
        auto start = chrono::steady_clock::now();

        // Задержка перед ходом из настроек (BotDelayMS)
        auto delay_ms = config("Bot", "BotDelayMS");
        // Задержку запускаем в отдельном потоке параллельно с поиском хода:
        // бот думает и «ждёт» одновременно, поэтому пауза перед ходом одинаковая —
        // не меньше delay_ms, даже если поиск занял меньше времени
        thread th(SDL_Delay, delay_ms);
        // Ищем лучший ход минимаксом. Результат — последовательность перемещений:
        // одно, если ход без взятия, или несколько, если это серия взятий
        auto turns = logic.find_best_turns(color);
        // Ждём окончания задержки
        th.join();

        bool is_first = true;
        // Выполняем ходы по очереди; между взятиями в серии — пауза delay_ms,
        // чтобы игрок успел увидеть каждое перемещение
        for (auto turn : turns)
        {
            if (!is_first)
            {
                SDL_Delay(delay_ms);
            }
            is_first = false;
            // Считаем взятия в серии (нужно для истории и корректного отката)
            beat_series += (turn.xb != -1);
            board.move_piece(turn, beat_series);
        }

        // Записываем в лог время хода бота
        auto end = chrono::steady_clock::now();
        ofstream fout(project_path + "log.txt", ios_base::app);
        fout << "Bot turn time: " << (int)chrono::duration<double, milli>(end - start).count() << " millisec\n";
        fout.close();
    }

    // Ход человека за цвет color (0 — белые, 1 — чёрные).
    // Возвращает OK, если ход сделан, или BACK / REPLAY / QUIT, если игрок нажал кнопку или закрыл окно
    // (тогда ход прерывается, а реакцию на это выполняет play())
    Response player_turn(const bool color)
    {
        // logic.turns уже заполнен в play() через find_turns(color).
        // Подсвечиваем все шашки, которыми можно сходить
        // (если есть обязательное взятие — только те, что могут бить)
        vector<pair<POS_T, POS_T>> cells;
        for (auto turn : logic.turns)
        {
            cells.emplace_back(turn.x, turn.y);
        }
        board.highlight_cells(cells);

        move_pos pos = {-1, -1, -1, -1}; // выбранный ход; pos.x == -1 — ход ещё не выбран
        POS_T x = -1, y = -1;            // выбранная (активная) шашка; -1 — шашка не выбрана

        // Этап 1: выбор шашки и клетки, куда ходить
        while (true)
        {
            auto resp = hand.get_cell();
            // Клик не по доске (кнопка «назад», «заново» или закрытие окна) — прерываем ход
            if (get<0>(resp) != Response::CELL)
                return get<0>(resp);
            pair<POS_T, POS_T> cell{get<1>(resp), get<2>(resp)};

            // Проверяем, что означает клик:
            // - по шашке, у которой есть ход, — выбор (или перевыбор) шашки (is_correct);
            // - по клетке, куда может сходить уже выбранная шашка, — выбор хода (pos)
            bool is_correct = false;
            for (auto turn : logic.turns)
            {
                if (turn.x == cell.first && turn.y == cell.second)
                {
                    is_correct = true;
                    break;
                }
                if (turn == move_pos{x, y, cell.first, cell.second})
                {
                    pos = turn;
                    break;
                }
            }
            // Ход выбран — выходим из цикла выбора
            if (pos.x != -1)
                break;
            // Клик по неподходящей клетке: снимаем выбор шашки и возвращаем исходную подсветку
            if (!is_correct)
            {
                if (x != -1)
                {
                    board.clear_active();
                    board.clear_highlight();
                    board.highlight_cells(cells);
                }
                x = -1;
                y = -1;
                continue;
            }
            // Выбрана шашка: делаем её активной и подсвечиваем клетки, куда она может пойти
            x = cell.first;
            y = cell.second;
            board.clear_highlight();
            board.set_active(x, y);
            vector<pair<POS_T, POS_T>> cells2;
            for (auto turn : logic.turns)
            {
                if (turn.x == x && turn.y == y)
                {
                    cells2.emplace_back(turn.x2, turn.y2);
                }
            }
            board.highlight_cells(cells2);
        }

        // Этап 2: выполняем выбранный ход. Второй аргумент move_piece —
        // номер взятия в серии (1, если ход со взятием, иначе 0); он нужен для отката хода
        board.clear_highlight();
        board.clear_active();
        board.move_piece(pos, pos.xb != -1);
        // Ход без взятия — ход завершён
        if (pos.xb == -1)
            return Response::OK;

        // Этап 3: серия взятий. По правилам после взятия та же шашка
        // обязана бить дальше, пока есть возможность
        beat_series = 1;
        while (true)
        {
            // Ищем ходы только для шашки, которая только что била
            logic.find_turns(pos.x2, pos.y2);
            // Бить больше нечего — ход завершён
            if (!logic.have_beats)
                break;

            // Подсвечиваем клетки, куда можно продолжить взятие; бьющая шашка остаётся активной
            vector<pair<POS_T, POS_T>> cells;
            for (auto turn : logic.turns)
            {
                cells.emplace_back(turn.x2, turn.y2);
            }
            board.highlight_cells(cells);
            board.set_active(pos.x2, pos.y2);

            // Ждём клик по одной из подсвеченных клеток; клики по другим клеткам игнорируются
            while (true)
            {
                auto resp = hand.get_cell();
                // Кнопки и закрытие окна работают и посреди серии (BACK откатит все взятия этого хода)
                if (get<0>(resp) != Response::CELL)
                    return get<0>(resp);
                pair<POS_T, POS_T> cell{get<1>(resp), get<2>(resp)};

                bool is_correct = false;
                for (auto turn : logic.turns)
                {
                    if (turn.x2 == cell.first && turn.y2 == cell.second)
                    {
                        is_correct = true;
                        pos = turn;
                        break;
                    }
                }
                if (!is_correct)
                    continue;

                // Выполняем очередное взятие и увеличиваем счётчик серии
                board.clear_highlight();
                board.clear_active();
                beat_series += 1;
                board.move_piece(pos, beat_series);
                break;
            }
        }

        return Response::OK;
    }

  private:
    Config config;
    Board board;
    Hand hand;
    Logic logic;
    int beat_series;
    bool is_replay = false;
};
