// Benchmark harness for the two debt items under review:
//   1. model::evaluateTrack() — per-call copy + stable_sort of the whole track.
//   2. next::ProjectDocument commit validation — per-edit deep copy/scan.
//
// Not wired into CMake on purpose (the debt task forbids CMake edits). Build it
// by hand against any configured build dir, e.g.:
//
//   g++ -std=c++20 -O2 -fPIC bench/debt_perf_bench.cpp \
//     -I src $(pkg-config --cflags Qt6Core) \
//     build-debt-perf/src/next/app/libpvf_next_document.a \
//     build-debt-perf/src/next/core/libpvf_next_core.a \
//     build-debt-perf/src/store/libpvf_store.a \
//     build-debt-perf/src/assets/libpvf_assets.a \
//     build-debt-perf/src/model/libpvf_model.a \
//     $(pkg-config --libs Qt6Core) -o /tmp/opencode/debt_perf_bench
//
// Usage: debt_perf_bench [model|document|all] [sample-scale]

#include "model/Keyframe.h"
#include "model/Project.h"
#include "model/Uuid.h"
#include "next/app/ProjectDocument.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

using namespace pvf;

namespace {

double g_sink = 0.0; // keeps the optimiser from deleting the work

// ---------------------------------------------------------------- timing

struct Stats
{
    double best = 0.0;
    double median = 0.0;
};

double median(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    return values.at(values.size() / 2);
}

// Runs `body` ntrial times, each trial over `iterations` calls, and returns the
// best/median per-call cost in microseconds.
Stats measure(const std::function<void(long)>& body, long iterations, int ntrial = 3)
{
    std::vector<double> samples;
    samples.reserve(ntrial);
    for (int i = 0; i < ntrial; ++i)
    {
        QElapsedTimer timer;
        timer.start();
        body(iterations);
        samples.push_back(double(timer.nsecsElapsed()) / 1000.0 / double(iterations));
    }
    Stats stats;
    stats.best = *std::min_element(samples.begin(), samples.end());
    stats.median = median(samples);
    return stats;
}

// ---------------------------------------------------------------- tracks

model::Keyframe key(int frame, double value)
{
    model::Keyframe k;
    k.frame = frame;
    k.value = value;
    return k;
}

model::Track makeTrack(int count, bool sorted)
{
    model::Track track;
    track.keys.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        // A third of the keys use a bezier easing so evaluation exercises the
        // eased path instead of only linear stepping.
        model::Keyframe k = key(i, double(i) * 1.5 - 4.0);
        if (i % 3 == 0)
        {
            k.easing.type = QStringLiteral("bezier");
            k.easing.inX = 0.42;
            k.easing.inY = 0.0;
            k.easing.outX = 0.58;
            k.easing.outY = 1.0;
        }
        track.keys.append(k);
    }
    if (!sorted)
        std::reverse(track.keys.begin(), track.keys.end());
    return track;
}

std::vector<double> frames(int samples, double lo, double hi)
{
    std::vector<double> out;
    out.reserve(samples);
    unsigned int state = 12345u;
    for (int i = 0; i < samples; ++i)
    {
        state = state * 1664525u + 1013904223u;
        const double unit = double(state >> 8 & 0xffffff) / double(0xffffff);
        out.push_back(lo + (hi - lo) * unit);
    }
    return out;
}

int benchModel(int scale)
{
    struct Case
    {
        const char* name;
        model::Track track;
        double hi;
    };

    std::vector<Case> cases;
    cases.push_back({"track-sorted-100", makeTrack(100, true), 99.0});
    cases.push_back({"track-sorted-1000", makeTrack(1000, true), 999.0});
    cases.push_back({"track-unsorted-1000", makeTrack(1000, false), 999.0});

    const long iterations = 10000L * scale;
    std::printf("\n## model::evaluateTrack (10k calls per sample)\n");
    for (const Case& c : cases)
    {
        const std::vector<double> lookup = frames(4096, 0.0, c.hi);
        // Warm up: the first call pays any lazy allocation.
        g_sink += model::evaluateTrack(c.track, lookup.at(0)).value_or(0.0);

        const Stats numeric = measure(
            [&](long n) {
                double sum = 0.0;
                for (long i = 0; i < n; ++i)
                    sum += model::evaluateTrack(c.track, lookup.at(size_t(i) % lookup.size()))
                               .value_or(0.0);
                g_sink += sum;
            },
            iterations);
        const Stats step = measure(
            [&](long n) {
                double sum = 0.0;
                for (long i = 0; i < n; ++i)
                    sum += model::evaluateTrack(c.track,
                                                lookup.at(size_t(i) % lookup.size()), true)
                               .value_or(0.0);
                g_sink += sum;
            },
            iterations);
        std::printf("| %s (%d keys) | numeric | %.3f us/call |\n", c.name,
                    int(c.track.keys.size()), numeric.best);
        std::printf("| %s (%d keys) | step | %.3f us/call |\n", c.name,
                    int(c.track.keys.size()), step.best);
    }
    return 0;
}

// ---------------------------------------------------------------- document

model::Entity makeEntity(const QString& name, int end = 120)
{
    model::Entity entity;
    entity.uuid = model::newUuid();
    entity.name = name;
    entity.start = 0;
    entity.end = end;
    entity.transform.opacity = 1.0;
    entity.tracks.insert(QStringLiteral("x"), makeTrack(5, true));
    entity.tracks.insert(QStringLiteral("y"), makeTrack(3, true));
    entity.tracks.insert(QStringLiteral("rotation"), makeTrack(4, true));
    entity.tracks.insert(QStringLiteral("opacity"), makeTrack(2, true));
    return entity;
}

struct Doc
{
    std::unique_ptr<QTemporaryDir> dir;
    std::unique_ptr<next::ProjectDocument> document;
    std::vector<Uuid> uuids;
};

std::unique_ptr<Doc> makeDocument(int entityCount)
{
    auto doc = std::make_unique<Doc>();
    doc->dir = std::make_unique<QTemporaryDir>();
    if (!doc->dir->isValid())
        return nullptr;
    QString error;
    doc->document = next::ProjectDocument::create(
        doc->dir->path() + QStringLiteral("/bench.pvproj"), model::ProjectSettings(),
        &error, /*useGit=*/false);
    if (!doc->document)
    {
        std::printf("create failed: %s\n", qPrintable(error));
        return nullptr;
    }
    auto& project = doc->document->mutableProject();
    for (int i = 0; i < entityCount; ++i)
    {
        const model::Entity entity = makeEntity(QStringLiteral("实体 %1").arg(i));
        doc->uuids.push_back(entity.uuid);
        project.entities.insert(entity.uuid, entity);
    }
    QVector<Uuid> all(doc->uuids.begin(), doc->uuids.end());
    if (!doc->document->commitEntities(all, QStringLiteral("bench: setup"), &error))
    {
        std::printf("setup commit failed: %s\n", qPrintable(error));
        return nullptr;
    }
    doc->document->flush(&error);
    return doc;
}

int benchDocument(int scale)
{
    constexpr int kEntities = 100;
    auto doc = makeDocument(kEntities);
    if (!doc)
        return 1;

    const long iterations = 500L * scale;
    std::printf("\n## next::ProjectDocument (%d entities, 4 tracks each)\n",
                kEntities);

    // An ordinary panel edit: mutate one entity, then commit that entity.
    const Stats commitEntity = measure(
        [&](long n) {
            for (long i = 0; i < n; ++i)
            {
                const Uuid& uuid = doc->uuids.at(size_t(i) % doc->uuids.size());
                model::Entity& entity = doc->document->mutableProject().entities[uuid];
                entity.tracks[QStringLiteral("x")].keys[0].value += 0.5;
                QString error;
                if (!doc->document->commitEntity(uuid, QStringLiteral("bench: edit"),
                                                 &error))
                {
                    std::printf("commitEntity failed: %s\n", qPrintable(error));
                    std::_Exit(2);
                }
            }
        },
        iterations);

    // Project settings save (commitProject validates the whole tree).
    const Stats commitProject = measure(
        [&](long n) {
            for (long i = 0; i < n; ++i)
            {
                QString error;
                if (!doc->document->commitProject(QStringLiteral("bench: project"),
                                                  &error))
                {
                    std::printf("commitProject failed: %s\n", qPrintable(error));
                    std::_Exit(2);
                }
            }
        },
        std::max(1L, iterations / 5));

    std::printf("| commitEntity (1 entity edited) | %.3f ms/op |\n",
                commitEntity.best / 1000.0);
    std::printf("| commitProject (settings save) | %.3f ms/op |\n",
                commitProject.best / 1000.0);

    // Delete one entity at a time. Each trial needs a fresh project because a
    // delete cannot be repeated on the same store.
    double deleteBest = 0.0;
    for (int trial = 0; trial < 3; ++trial)
    {
        auto deleteDoc = makeDocument(kEntities);
        if (!deleteDoc)
            return 1;
        QElapsedTimer timer;
        timer.start();
        for (const Uuid& uuid : deleteDoc->uuids)
        {
            QString error;
            if (!deleteDoc->document->deleteEntities({uuid},
                                                     QStringLiteral("bench: delete"),
                                                     &error))
            {
                std::printf("deleteEntities failed: %s\n", qPrintable(error));
                std::_Exit(2);
            }
        }
        const double perOp = double(timer.nsecsElapsed()) / 1000.0
                             / double(deleteDoc->uuids.size());
        if (deleteBest == 0.0 || perOp < deleteBest)
            deleteBest = perOp;
    }
    std::printf("| deleteEntities (1 of 100) | %.3f ms/op |\n", deleteBest / 1000.0);

    // Raw model validation of the same project, no copy: isolates the scan.
    const model::Project& project = doc->document->project();
    const Stats validate = measure(
        [&](long n) {
            for (long i = 0; i < n; ++i)
            {
                QString error;
                if (!model::validateProject(project, &error))
                {
                    std::printf("validateProject failed: %s\n", qPrintable(error));
                    std::_Exit(2);
                }
            }
        },
        std::max(1L, iterations / 5));
    std::printf("| model::validateProject (no copy) | %.3f ms/op |\n",
                validate.best / 1000.0);

    // Copy + validation, the shape the current validateForWrite() uses.
    const Stats copyValidate = measure(
        [&](long n) {
            for (long i = 0; i < n; ++i)
            {
                model::Project check = project;
                const Uuid& uuid = doc->uuids.at(size_t(i) % doc->uuids.size());
                check.entities.remove(uuid);
                QString error;
                if (!model::validateProject(check, &error))
                {
                    std::printf("copy validate failed: %s\n", qPrintable(error));
                    std::_Exit(2);
                }
            }
        },
        std::max(1L, iterations / 5));
    std::printf("| copy + remove + validateProject | %.3f ms/op |\n",
                copyValidate.best / 1000.0);

    // Store write alone (entity validation + JSON + QSaveFile): the part of an
    // edit that no validation change can remove.
    const Stats saveEntity = measure(
        [&](long n) {
            for (long i = 0; i < n; ++i)
            {
                const Uuid& uuid = doc->uuids.at(size_t(i) % doc->uuids.size());
                QString error;
                if (!doc->document->store()->saveEntity(uuid, &error))
                {
                    std::printf("saveEntity failed: %s\n", qPrintable(error));
                    std::_Exit(2);
                }
            }
        },
        std::max(1L, iterations / 5));
    std::printf("| store->saveEntity (write only) | %.3f ms/op |\n",
                saveEntity.best / 1000.0);

    // Per-entity validation alone, to see how much of validateProject is the
    // entity scan.
    const Stats entitiesOnly = measure(
        [&](long n) {
            for (long i = 0; i < n; ++i)
            {
                QString error;
                for (auto it = project.entities.constBegin();
                     it != project.entities.constEnd(); ++it)
                {
                    if (!model::validateEntity(it.value(), &error))
                    {
                        std::printf("validateEntity failed: %s\n", qPrintable(error));
                        std::_Exit(2);
                    }
                }
            }
        },
        std::max(1L, iterations / 20));
    std::printf("| validateEntity x %d only | %.3f ms/op |\n", kEntities,
                entitiesOnly.best / 1000.0);

    // Hash iteration baseline: one pass reading a field from every entity.
    const Stats iterate = measure(
        [&](long n) {
            for (long i = 0; i < n; ++i)
            {
                long sum = 0;
                for (auto it = project.entities.constBegin();
                     it != project.entities.constEnd(); ++it)
                    sum += it.value().start + it.value().end;
                g_sink += double(sum);
            }
        },
        std::max(1L, iterations / 20));
    std::printf("| hash iterate x %d only | %.3f ms/op |\n", kEntities,
                iterate.best / 1000.0);

    // Same entity count, all parented into one deep chain: exercises the parent
    // range pass and the parent-cycle walk instead of 100 one-node chains.
    model::Project chained = project;
    chained.entities.clear();
    Uuid previous;
    for (int i = 0; i < kEntities; ++i)
    {
        model::Entity entity = makeEntity(QStringLiteral("链 %1").arg(i));
        entity.parentUuid = previous;
        previous = entity.uuid;
        chained.entities.insert(entity.uuid, entity);
    }
    const Stats chainedValidate = measure(
        [&](long n) {
            for (long i = 0; i < n; ++i)
            {
                QString error;
                if (!model::validateProject(chained, &error))
                {
                    std::printf("chained validate failed: %s\n", qPrintable(error));
                    std::_Exit(2);
                }
            }
        },
        std::max(1L, iterations / 20));
    std::printf("| validateProject, %d-deep parent chain | %.3f ms/op |\n",
                kEntities, chainedValidate.best / 1000.0);

    // And with no entities at all: fixed overhead (settings, markers).
    model::Project empty = project;
    empty.entities.clear();
    const Stats emptyValidate = measure(
        [&](long n) {
            for (long i = 0; i < n; ++i)
            {
                QString error;
                if (!model::validateProject(empty, &error))
                {
                    std::printf("empty validate failed: %s\n", qPrintable(error));
                    std::_Exit(2);
                }
            }
        },
        std::max(1L, iterations / 20));
    std::printf("| validateProject, no entities | %.3f ms/op |\n",
                emptyValidate.best / 1000.0);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QString mode = argc > 1 ? QString::fromLatin1(argv[1]) : QStringLiteral("all");
    const int scale = argc > 2 ? std::max(1, std::atoi(argv[2])) : 1;

    std::printf("# PVForge debt perf bench\n");
    if (mode == QLatin1String("model") || mode == QLatin1String("all"))
    {
        if (benchModel(scale) != 0)
            return 1;
    }
    if (mode == QLatin1String("document") || mode == QLatin1String("all"))
    {
        if (benchDocument(scale) != 0)
            return 1;
    }
    std::printf("\n(sink %.3f)\n", g_sink);
    return 0;
}
