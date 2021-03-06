// Copyright (c) 2017-2017 Josh Blum
// SPDX-License-Identifier: BSL-1.0

#pragma once
#include <Pothos/Config.hpp>
#include <QString>
#include <mutex>
#include <deque>
#include <QFileInfo>
#include <QtGlobal> //Q_FUNC_INFO

//! The eval tracer keeps track of a stack (thread safe)
class EvalTracer
{
public:
    EvalTracer(void);

    //! Get a formated printable string
    QString trace(void) const;

    //! Push a new position onto the top of the stack
    void push(const QString &what);

    //! Remove the element from the top of the stack
    void pop(void);

    //! Set the local thread context's tracer
    static void install(EvalTracer &tracer);

    /*!
     * Get the local thread context's tracer.
     * See the macros below for usage.
     */
    static EvalTracer &getGlobal(void);

private:
    mutable std::mutex _mutex;
    std::deque<QString> _stack;
};

//! Create an entry in the tracer that cleans itself up
class EvalTraceEntry
{
public:
    EvalTraceEntry(EvalTracer &stack, const QString &what);

    ~EvalTraceEntry(void);

private:
    EvalTracer &_stack;
};

#define __CONCAT_IMPL( x, y ) x##y
#define __MACRO_CONCAT( x, y ) __CONCAT_IMPL( x, y )

//! Create an entry in the tracer for an arbitrary action
#define EVAL_TRACER_ACTION(a) EvalTraceEntry \
    __MACRO_CONCAT(__evalTraceEntry, __COUNTER__)( \
        EvalTracer::getGlobal(), QString("%1:%2 %3") \
        .arg(QFileInfo(__FILE__).fileName()).arg(__LINE__).arg(a))

//! Create an entry in the tracer for entering a function
#define EVAL_TRACER_FUNC() EVAL_TRACER_ACTION(Q_FUNC_INFO)

//! Provide an extra argument that identifies the object
#define EVAL_TRACER_FUNC_ARG(what) \
    EVAL_TRACER_ACTION(QString("%1 [%2]").arg(Q_FUNC_INFO).arg(what))
