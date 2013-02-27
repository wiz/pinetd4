#include <mod/core/Core.hpp>
#include <mod/core/CoreTcp.hpp>
#include <mod/core/Daemon.hpp>
#include <QStringList>
#include <QSet>

Core::Core(): settings("pinetd.ini", QSettings::IniFormat) {
	qDebug("Core: in constructor");
	reloadConfig();
}

void Core::reloadConfig() {
	qDebug("Core: reloading configuration");
	settings.beginGroup("daemons");
	QStringList k = settings.allKeys();

	QSet<QString> cur_daemons;
	// iterate on currently loaded daemons
	for(auto i = daemons.constBegin(); i != daemons.constEnd(); i++)
		cur_daemons.insert(i.key());

	for(int i = 0; i < k.size(); i++) {
		if (daemons.contains(k.at(i))) {
			// already loaded and still in config, ignore
			// TODO: we may want to check the module is still the same
			cur_daemons.remove(k.at(i));
			continue;
		}
		QString mod_name = settings.value(k.at(i)).toString();
		qDebug("Core: loading daemon: %s module %s", qPrintable(k.at(i)), qPrintable(mod_name));
		if (!modprobe(mod_name)) continue;

		// _Z18pinetd_instanciateRK7QStringS1_ = pinetd_instanciate(QString const&, QString const&)
		Daemon *(*callback)(const QString &, const QString &);
		callback = (Daemon*(*)(const QString &, const QString &))modules.value(mod_name)->resolve("_Z18pinetd_instanciateRK7QStringS1_");
		if (callback == NULL) {
			qDebug("Core: failed to load daemon %s as it has no instanciate method", qPrintable(k.at(i)));
			continue;
		}
		Daemon *d = callback(mod_name, k.at(i));
		if (d == NULL) {
			qDebug("Core: failed to instanciate daemon %s", qPrintable(k.at(i)));
			continue;
		}
		daemons.insert(k.at(i), d);
	}

	for(auto i = cur_daemons.constBegin(); i != cur_daemons.constEnd(); i++) {
		Daemon *d = daemons.take(*i);
		// we may want to set a signal on d
		d->deleteLater();
	}

	settings.endGroup();

	settings.beginGroup("tcp");
	k = settings.allKeys();

	QSet<QString> cur_tcp;
	// iterate on currently routed tcp ports
	for(auto i = port_tcp.constBegin(); i != port_tcp.constEnd(); i++)
		cur_tcp.insert(i.key());

	for(int i = 0; i < k.size(); i++) {
		if (port_tcp.contains(k.at(i))) {
			// already linked to somewhere, remove and re-add with new value
			port_tcp.value(k.at(i))->setTarget(settings.value(k.at(i)).toString());
			cur_tcp.remove(k.at(i));
			continue;
		}
		CoreTcp *t = new CoreTcp(this);
		t->setTarget(settings.value(k.at(i)).toString());
		QStringList nfo = k.at(i).split(':');
		if (nfo.size() != 2) {
			qDebug("Failed to listen on %s, bad syntax", qPrintable(k.at(i)));
			continue;
		}
		if (!t->listen(QHostAddress(nfo.at(0)), nfo.at(1).toInt())) {
			qDebug("Failed to listen on %s, giving up", qPrintable(k.at(i)));
			continue;
		}
		port_tcp.insert(k.at(i), t);
	}

	for(auto i = cur_tcp.constBegin(); i != cur_tcp.constEnd(); i++)
		port_tcp.take(*i)->deleteLater();

	settings.endGroup();
}

bool Core::modprobe(const QString &name) {
	if (modules.contains(name)) return true;

	qDebug("Core: loading module %s", qPrintable(name));

	QLibrary *lib = new QLibrary(QString("mod/")+name, this);
	lib->setLoadHints(QLibrary::ResolveAllSymbolsHint);
	if (!lib->load()) {
		qDebug("Core: failed to load module: %s", qPrintable(lib->errorString()));
		delete lib;
		return false;
	}

	modules.insert(name, lib);
	return true;
}

