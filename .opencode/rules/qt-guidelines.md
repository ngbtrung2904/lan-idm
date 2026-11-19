# Qt Widgets / QML Guidelines

## General

1. Composition first. Build complex widgets from smaller, reusable primitives.
2. Use existing shared widgets or QML components before creating new ones.
3. Do not add feature-specific wrappers unless they remove repeated complexity across 2+ real consumers.
4. Match known-good in-repo usage before inventing a new composition pattern.
5. Do not reimplement standard interactions (menus, dialogs, drag-drop) if Qt or the project already provides them.

## Qt Widgets

1. Prefer layouts (`QHBoxLayout`, `QVBoxLayout`, `QGridLayout`) over manual geometry.
2. Set proper size policies, minimum/maximum sizes, and stretch factors for responsive layouts.
3. Use `Q_PROPERTY` for properties that should be accessible from QML or the meta-object system.
4. Signals must be emitted with typed parameters — avoid untyped `QVariant` unless necessary.
5. Slots should be small and focused; delegate heavy work to worker objects or threads.
6. Use `Q_DISABLE_COPY` for classes that manage resources or have identity.

## QML

1. Keep QML files focused — extract reusable components into separate files.
2. Prefer `property` bindings over imperative JavaScript assignments.
3. Use `QtObject` or `Item` as base types for non-visual or visual components respectively.
4. Avoid deep property binding chains that are hard to debug.
5. Use `Connections` type for cross-component signal handling rather than direct signal handlers on foreign objects.
6. Prefer `ListModel` / `ListElement` for static data; use C++ models for dynamic or large datasets.

## Threading

1. Never block the GUI thread. Use `QThread`, `QtConcurrent`, or async patterns for long-running work.
2. Use signals/slots or `QMetaObject::invokeMethod` with `Qt::QueuedConnection` for cross-thread communication.
3. Worker objects should live in their own thread — do not move `QObject`s with children across threads.
