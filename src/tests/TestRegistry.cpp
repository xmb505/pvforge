#include "TestRegistry.h"

#include <QApplication>
#include <QDebug>
#include <QStringList>

namespace pvf::tests {

void Registry::add(const QString& name, Handler handler)
{
    m_handlers.insert(name, std::move(handler));
}

bool Registry::contains(const QString& name) const
{
    return m_handlers.contains(name);
}

std::optional<int> Registry::run(QApplication& app) const
{
    const QStringList arguments = app.arguments();
    for (int i = 1; i < arguments.size(); ++i)
    {
        const auto it = m_handlers.constFind(arguments.at(i));
        if (it != m_handlers.constEnd())
            return (*it)(app);
    }
    return std::nullopt;
}

namespace {

Registry buildRegistry()
{
    Registry registry;
    registerSessionTests(registry);
    registerPropertiesTests(registry);
    registerSolverTests(registry);
    registerProfileFixtures(registry);
    registerTimelineTests(registry);
    registerViewportCameraTests(registry);
    registerEditorTests(registry);
    registerRenderMediaTests(registry);
    registerAssetTests(registry);
    registerMcpTests(registry);
    registerPerfTests(registry);
    return registry;
}

} // namespace

bool isTestRun(const QStringList& arguments)
{
    const Registry registry = buildRegistry();
    for (const QString& argument : arguments)
    {
        if (registry.contains(argument))
            return true;
    }
    return false;
}

std::optional<int> dispatch(QApplication& app)
{
    Registry registry = buildRegistry();
    if (const auto result = registry.run(app))
        return result;

    // A `--*-test` switch that nothing registers is a retired or misspelled
    // switch (issue #72 retired several). Fail loudly here; falling through to
    // the welcome window would look like a hang to scripts and CI.
    for (const QString& argument : app.arguments())
    {
        if (argument.startsWith(QStringLiteral("--"))
            && argument.endsWith(QStringLiteral("-test")))
        {
            qCritical().noquote()
                << "unknown test switch:" << argument
                << "(retired or misspelled; see src/tests/TestRegistry.cpp)";
            return 1;
        }
    }
    return std::nullopt;
}

} // namespace pvf::tests
