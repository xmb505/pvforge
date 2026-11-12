#pragma once

// Registry for the in-binary self-tests.
//
// Production `main()` asks the registry whether one of the `--*-test` switches
// was passed; when the build has no tests (release), main() never references
// this file at all (PVFORGE_BUILD_SELF_TESTS is off and pvforge-tests is the
// only target that compiles src/tests/).

#include <QHash>
#include <QString>
#include <functional>
#include <optional>

class QApplication;

namespace pvf::tests {

class Registry
{
public:
    using Handler = std::function<int(QApplication&)>;

    void add(const QString& name, Handler handler);

    // Runs the handler for the first matching `--*-test`-style switch in the
    // process arguments. Returns nullopt when none matches, so main() can fall
    // through to the normal CLI / welcome window path.
    std::optional<int> run(QApplication& app) const;

private:
    QHash<QString, Handler> m_handlers;
};

// One entry point per test translation unit, defined next to the tests it
// registers. TestRegistry.cpp owns the list so main() stays a single call.
void registerSessionTests(Registry& registry);
void registerPropertiesTests(Registry& registry);
void registerSolverTests(Registry& registry);
void registerProfileFixtures(Registry& registry);
void registerTimelineTests(Registry& registry);
void registerViewportCameraTests(Registry& registry);
void registerEditorTests(Registry& registry);
void registerSceneTests(Registry& registry);
void registerWorkbenchTests(Registry& registry);
void registerRenderMediaTests(Registry& registry);
void registerAssetTests(Registry& registry);
void registerMcpTests(Registry& registry);
void registerPerfTests(Registry& registry);

// Called from main() when tests are compiled in.
std::optional<int> dispatch(QApplication& app);

} // namespace pvf::tests
