#include <string>
#include <vector>

namespace demo {

// A type the oracle should see as TypeLike.
class Widget {
  public:
    explicit Widget(std::string name) : name_(std::move(name)) {
    }

    const std::string& Name() const {
        return name_;
    }

  private:
    std::string name_;
};

int Total(const std::vector<int>& values) {
    int sum = 0;
    for (int v : values) {
        sum += v;
    }
    return sum;
}

} // namespace demo
