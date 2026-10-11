#pragma once
#include <stdlib.h>

// Тип для координат на доске (0..7, -1 — «нет значения»); int8_t экономит память
typedef int8_t POS_T;

// Описание одного хода (одного перемещения шашки)
struct move_pos
{
    POS_T x, y;             // откуда: строка и столбец
    POS_T x2, y2;           // куда: строка и столбец
    POS_T xb = -1, yb = -1; // координаты побитой шашки; -1 — ход без взятия

    // Ход без взятия
    move_pos(const POS_T x, const POS_T y, const POS_T x2, const POS_T y2) : x(x), y(y), x2(x2), y2(y2)
    {
    }
    // Ход со взятием шашки на клетке (xb, yb)
    move_pos(const POS_T x, const POS_T y, const POS_T x2, const POS_T y2, const POS_T xb, const POS_T yb)
        : x(x), y(y), x2(x2), y2(y2), xb(xb), yb(yb)
    {
    }

    // Ходы равны, если совпадают клетки «откуда» и «куда» (побитая шашка не сравнивается)
    bool operator==(const move_pos &other) const
    {
        return (x == other.x && y == other.y && x2 == other.x2 && y2 == other.y2);
    }
    bool operator!=(const move_pos &other) const
    {
        return !(*this == other);
    }
};