#pragma once
#include <fstream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "../Models/Project_path.h"

// Класс-обёртка над настройками игры из settings.json.
// Хранит разобранный JSON и даёт к нему короткий доступ через operator().
class Config
{
public:
    // Настройки читаются сразу при создании объекта
    Config()
    {
        reload();
    }

    // reload() заново читает settings.json с диска и заменяет текущие настройки.
    // Вызывается в конструкторе и в Game::play() при повторной партии (кнопка replay),
    // поэтому изменения в settings.json применяются без перезапуска программы.
    // Путь строится от project_path (см. Project_path.h).
    // Флаг ignore_comments позволяет держать в settings.json комментарии //,
    // которых нет в стандартном JSON.
    void reload()
    {
        std::ifstream fin(project_path + "settings.json");
        config = json::parse(fin, nullptr, true, /* ignore_comments = */ true);
        fin.close();
    }

    // Оператор () перегружен, чтобы обращаться к настройке коротко, как к функции:
    //     config("Bot", "IsWhiteBot")   вместо   config.config["Bot"]["IsWhiteBot"]
    // setting_dir — раздел (верхний ключ: "WindowSize", "Bot", "Game"),
    // setting_name — имя настройки внутри раздела.
    // Сам json остаётся private: снаружи настройки можно только читать (метод const).
    // Возвращается json-значение, которое неявно приводится к нужному типу
    // (int, bool, string) в месте использования.
    auto operator()(const string &setting_dir, const string &setting_name) const
    {
        return config[setting_dir][setting_name];
    }

private:
    json config; // разобранное содержимое settings.json
};