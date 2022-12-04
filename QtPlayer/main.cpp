#include "mainwindow.h"

#include <QApplication>

#include <boost/log/sinks/debug_output_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/core/core.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include <boost/log/expressions.hpp>

#include <boost/log/trivial.hpp>

#include <boost/log/support/date_time.hpp>

static void init_logging()
{
    namespace expr = boost::log::expressions;

    boost::log::add_common_attributes();

    auto core = boost::log::core::get();

#ifdef Q_OS_WIN
    // Create the sink. The backend requires synchronization in the frontend.
    auto sink(boost::make_shared<boost::log::sinks::synchronous_sink<boost::log::sinks::debug_output_backend>>());
#else
    auto sink = boost::make_shared<boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend >>();
#endif

    sink->set_formatter(expr::stream
        //<< '[' << expr::format_date_time< boost::posix_time::ptime >("TimeStamp", "%H:%M:%S.%f") << ']'
        << expr::if_(expr::has_attr("Severity"))
        [
            expr::stream << '[' << expr::attr< boost::log::trivial::severity_level >("Severity") << ']'
        ]
    << expr::if_(expr::has_attr("Channel"))
        [
            expr::stream << '[' << expr::attr< std::string >("Channel") << ']'
        ]
    << expr::smessage << '\n');

    // Set the special filter to the frontend
    // in order to skip the sink when no debugger is available
    //sink->set_filter(expr::is_debugger_present());

    core->add_sink(sink);
}

int main(int argc, char *argv[])
{
    init_logging();

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return QApplication::exec();
}
