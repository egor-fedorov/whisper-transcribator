#include "support/fixtures/models.hpp"

namespace wt::test {
Model model_fixture() {
    return {"fixture", "fixture.bin", "https://example.invalid/model",
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 3};
}
} // namespace wt::test
