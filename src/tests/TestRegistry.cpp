#include "TestRegistry.h"

#include <QApplication>
#include <QStringList>

namespace pvf::tests {

void Registry::add(const QString& name, Handler handler)
{
    m_handlers.insert(name, std::move(handler));
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

std::optional<int> dispatch(QApplication& app)
{
    Registry registry;
    registerSessionTests(registry);
    registerPropertiesTests(registry);
    registerSolverTests(registry);
    registerProfileFixtures(registry);
    registerTimelineTests(registry);
    registerViewportCameraTests(registry);
    registerEditorTests(registry);
    registerSceneTests(registry);
    registerWorkbenchTests(registry);
    registerRenderMediaTests(registry);
    registerAssetTests(registry);
    registerMcpTests(registry);
    registerPerfTests(registry);
    return registry.run(app);
}

} // namespace pvf::tests
