#include <easel/easel.h>
using namespace easel;

int main(int argc, char** argv) {
    App app(argc, argv);
    app.title("MySketch");
    app.onDraw([](Canvas& c) {
        c.text({0, 0}, "Hello, Easel", Align::Center);
    });
    return app.run();
}
