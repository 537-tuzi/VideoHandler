/****************************************************************************
** Meta object code from reading C++ file 'mediaservice.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../../../vh-prefix/src/service/mediaservice.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'mediaservice.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN12MediaServiceE_t {};
} // unnamed namespace

template <> constexpr inline auto MediaService::qt_create_metaobjectdata<qt_meta_tag_ZN12MediaServiceE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "MediaService",
        "audioRequested",
        "",
        "inPath",
        "videoRequested",
        "probeRequested",
        "denoiseRequested",
        "removeHum",
        "removeWind",
        "aiDenoiseRequested",
        "pitchRequested",
        "semiTones",
        "sepVocalsRequested",
        "instrumental",
        "progress",
        "pct",
        "jobFinished",
        "ok",
        "message",
        "probeFinished",
        "info",
        "probe",
        "extractAudio",
        "extractVideo",
        "denoiseWav",
        "denoiseAiWav",
        "pitchWav",
        "sepVocals"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'audioRequested'
        QtMocHelpers::SignalData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'videoRequested'
        QtMocHelpers::SignalData<void(const QString &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'probeRequested'
        QtMocHelpers::SignalData<void(const QString &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'denoiseRequested'
        QtMocHelpers::SignalData<void(const QString &, bool, bool)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::Bool, 7 }, { QMetaType::Bool, 8 },
        }}),
        // Signal 'aiDenoiseRequested'
        QtMocHelpers::SignalData<void(const QString &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'pitchRequested'
        QtMocHelpers::SignalData<void(const QString &, double)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::Double, 11 },
        }}),
        // Signal 'sepVocalsRequested'
        QtMocHelpers::SignalData<void(const QString &, bool)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::Bool, 13 },
        }}),
        // Signal 'progress'
        QtMocHelpers::SignalData<void(int)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 15 },
        }}),
        // Signal 'jobFinished'
        QtMocHelpers::SignalData<void(bool, const QString &)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 17 }, { QMetaType::QString, 18 },
        }}),
        // Signal 'probeFinished'
        QtMocHelpers::SignalData<void(bool, const QString &)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 17 }, { QMetaType::QString, 20 },
        }}),
        // Slot 'probe'
        QtMocHelpers::SlotData<void(const QString &)>(21, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Slot 'extractAudio'
        QtMocHelpers::SlotData<void(const QString &)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Slot 'extractVideo'
        QtMocHelpers::SlotData<void(const QString &)>(23, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Slot 'denoiseWav'
        QtMocHelpers::SlotData<void(const QString &, bool, bool)>(24, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::Bool, 7 }, { QMetaType::Bool, 8 },
        }}),
        // Slot 'denoiseAiWav'
        QtMocHelpers::SlotData<void(const QString &)>(25, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Slot 'pitchWav'
        QtMocHelpers::SlotData<void(const QString &, double)>(26, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::Double, 11 },
        }}),
        // Slot 'sepVocals'
        QtMocHelpers::SlotData<void(const QString &, bool)>(27, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::Bool, 13 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<MediaService, qt_meta_tag_ZN12MediaServiceE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject MediaService::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12MediaServiceE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12MediaServiceE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN12MediaServiceE_t>.metaTypes,
    nullptr
} };

void MediaService::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MediaService *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->audioRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->videoRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->probeRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->denoiseRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3]))); break;
        case 4: _t->aiDenoiseRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 5: _t->pitchRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[2]))); break;
        case 6: _t->sepVocalsRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 7: _t->progress((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 8: _t->jobFinished((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 9: _t->probeFinished((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 10: _t->probe((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 11: _t->extractAudio((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 12: _t->extractVideo((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 13: _t->denoiseWav((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3]))); break;
        case 14: _t->denoiseAiWav((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 15: _t->pitchWav((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[2]))); break;
        case 16: _t->sepVocals((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (MediaService::*)(const QString & )>(_a, &MediaService::audioRequested, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (MediaService::*)(const QString & )>(_a, &MediaService::videoRequested, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (MediaService::*)(const QString & )>(_a, &MediaService::probeRequested, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (MediaService::*)(const QString & , bool , bool )>(_a, &MediaService::denoiseRequested, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (MediaService::*)(const QString & )>(_a, &MediaService::aiDenoiseRequested, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (MediaService::*)(const QString & , double )>(_a, &MediaService::pitchRequested, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (MediaService::*)(const QString & , bool )>(_a, &MediaService::sepVocalsRequested, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (MediaService::*)(int )>(_a, &MediaService::progress, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (MediaService::*)(bool , const QString & )>(_a, &MediaService::jobFinished, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (MediaService::*)(bool , const QString & )>(_a, &MediaService::probeFinished, 9))
            return;
    }
}

const QMetaObject *MediaService::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MediaService::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12MediaServiceE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int MediaService::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 17)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 17;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 17)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 17;
    }
    return _id;
}

// SIGNAL 0
void MediaService::audioRequested(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void MediaService::videoRequested(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void MediaService::probeRequested(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void MediaService::denoiseRequested(const QString & _t1, bool _t2, bool _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2, _t3);
}

// SIGNAL 4
void MediaService::aiDenoiseRequested(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void MediaService::pitchRequested(const QString & _t1, double _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2);
}

// SIGNAL 6
void MediaService::sepVocalsRequested(const QString & _t1, bool _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1, _t2);
}

// SIGNAL 7
void MediaService::progress(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}

// SIGNAL 8
void MediaService::jobFinished(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1, _t2);
}

// SIGNAL 9
void MediaService::probeFinished(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1, _t2);
}
QT_WARNING_POP
