#pragma once

#include <QString>

namespace pvf {

// Lightweight JSON translation table. Language files live in resources as
// :/lang/<code>.json and map keys to display strings.
namespace Lang {

void load(const QString& code);
QString t(const QString& key, const QString& fallback = QString());

} // namespace Lang

} // namespace pvf
