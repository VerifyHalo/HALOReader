/****************************************************************************
** Meta object code from reading C++ file 'seizure_analyzer.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.5.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../seizure_analyzer.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#if __has_include(<QtCore/qtmochelpers.h>)
#include <QtCore/qtmochelpers.h>
#else
QT_BEGIN_MOC_NAMESPACE
#endif


#include <memory>

#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'seizure_analyzer.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.5.3. It"
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

#ifdef QT_MOC_HAS_STRINGDATA
struct qt_meta_stringdata_CLASSSeizureAnalyzerENDCLASS_t {};
static constexpr auto qt_meta_stringdata_CLASSSeizureAnalyzerENDCLASS = QtMocHelpers::stringData(
    "SeizureAnalyzer",
    "reloadData",
    "",
    "updateDisplay",
    "onChannelItemChanged",
    "QListWidgetItem*",
    "item",
    "showChannelPopup",
    "onDailySelectionChanged",
    "selectDataFolder",
    "onDataDirectoryChanged",
    "path",
    "processNewRhdFiles",
    "onFileProcessed",
    "filePath",
    "success",
    "error"
);
#else  // !QT_MOC_HAS_STRING_DATA
struct qt_meta_stringdata_CLASSSeizureAnalyzerENDCLASS_t {
    uint offsetsAndSizes[34];
    char stringdata0[16];
    char stringdata1[11];
    char stringdata2[1];
    char stringdata3[14];
    char stringdata4[21];
    char stringdata5[17];
    char stringdata6[5];
    char stringdata7[17];
    char stringdata8[24];
    char stringdata9[17];
    char stringdata10[23];
    char stringdata11[5];
    char stringdata12[19];
    char stringdata13[16];
    char stringdata14[9];
    char stringdata15[8];
    char stringdata16[6];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_CLASSSeizureAnalyzerENDCLASS_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_CLASSSeizureAnalyzerENDCLASS_t qt_meta_stringdata_CLASSSeizureAnalyzerENDCLASS = {
    {
        QT_MOC_LITERAL(0, 15),  // "SeizureAnalyzer"
        QT_MOC_LITERAL(16, 10),  // "reloadData"
        QT_MOC_LITERAL(27, 0),  // ""
        QT_MOC_LITERAL(28, 13),  // "updateDisplay"
        QT_MOC_LITERAL(42, 20),  // "onChannelItemChanged"
        QT_MOC_LITERAL(63, 16),  // "QListWidgetItem*"
        QT_MOC_LITERAL(80, 4),  // "item"
        QT_MOC_LITERAL(85, 16),  // "showChannelPopup"
        QT_MOC_LITERAL(102, 23),  // "onDailySelectionChanged"
        QT_MOC_LITERAL(126, 16),  // "selectDataFolder"
        QT_MOC_LITERAL(143, 22),  // "onDataDirectoryChanged"
        QT_MOC_LITERAL(166, 4),  // "path"
        QT_MOC_LITERAL(171, 18),  // "processNewRhdFiles"
        QT_MOC_LITERAL(190, 15),  // "onFileProcessed"
        QT_MOC_LITERAL(206, 8),  // "filePath"
        QT_MOC_LITERAL(215, 7),  // "success"
        QT_MOC_LITERAL(223, 5)   // "error"
    },
    "SeizureAnalyzer",
    "reloadData",
    "",
    "updateDisplay",
    "onChannelItemChanged",
    "QListWidgetItem*",
    "item",
    "showChannelPopup",
    "onDailySelectionChanged",
    "selectDataFolder",
    "onDataDirectoryChanged",
    "path",
    "processNewRhdFiles",
    "onFileProcessed",
    "filePath",
    "success",
    "error"
};
#undef QT_MOC_LITERAL
#endif // !QT_MOC_HAS_STRING_DATA
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_CLASSSeizureAnalyzerENDCLASS[] = {

 // content:
      11,       // revision
       0,       // classname
       0,    0, // classinfo
       9,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   68,    2, 0x08,    1 /* Private */,
       3,    0,   69,    2, 0x08,    2 /* Private */,
       4,    1,   70,    2, 0x08,    3 /* Private */,
       7,    0,   73,    2, 0x08,    5 /* Private */,
       8,    0,   74,    2, 0x08,    6 /* Private */,
       9,    0,   75,    2, 0x08,    7 /* Private */,
      10,    1,   76,    2, 0x08,    8 /* Private */,
      12,    0,   79,    2, 0x08,   10 /* Private */,
      13,    3,   80,    2, 0x08,   11 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   11,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::Bool, QMetaType::QString,   14,   15,   16,

       0        // eod
};

Q_CONSTINIT const QMetaObject SeizureAnalyzer::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_CLASSSeizureAnalyzerENDCLASS.offsetsAndSizes,
    qt_meta_data_CLASSSeizureAnalyzerENDCLASS,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_CLASSSeizureAnalyzerENDCLASS_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<SeizureAnalyzer, std::true_type>,
        // method 'reloadData'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateDisplay'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onChannelItemChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QListWidgetItem *, std::false_type>,
        // method 'showChannelPopup'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onDailySelectionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'selectDataFolder'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onDataDirectoryChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'processNewRhdFiles'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onFileProcessed'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>
    >,
    nullptr
} };

void SeizureAnalyzer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<SeizureAnalyzer *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->reloadData(); break;
        case 1: _t->updateDisplay(); break;
        case 2: _t->onChannelItemChanged((*reinterpret_cast< std::add_pointer_t<QListWidgetItem*>>(_a[1]))); break;
        case 3: _t->showChannelPopup(); break;
        case 4: _t->onDailySelectionChanged(); break;
        case 5: _t->selectDataFolder(); break;
        case 6: _t->onDataDirectoryChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 7: _t->processNewRhdFiles(); break;
        case 8: _t->onFileProcessed((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[3]))); break;
        default: ;
        }
    }
}

const QMetaObject *SeizureAnalyzer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SeizureAnalyzer::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_CLASSSeizureAnalyzerENDCLASS.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int SeizureAnalyzer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 9;
    }
    return _id;
}
QT_WARNING_POP
