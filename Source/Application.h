//
// Created by Fester on 5/29/25.
//

#ifndef APPLICATION_H
#define APPLICATION_H

#include <mutex>
#include <string>
#include <iostream>

namespace Ned {
class Application {
  public:
    static std::string GetTitle();

    static void SetTitle(const std::string& title);
    static void SetTitle(const char* title);

  protected:
    static std::mutex& Mutex();
    static std::string Title(const std::string& title = "");
};
} // namespace Ned

#endif // APPLICATION_H
