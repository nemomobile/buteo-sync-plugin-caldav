#ifndef GET_H
#define GET_H

#include <request.h>

class Get : public Request
{
    Q_OBJECT

public:
    explicit Get(QNetworkAccessManager *manager, Settings *settings, QObject *parent = 0);

    void getEvent(const QString &u);

private Q_SLOTS:
    void requestFinished();
};

#endif // GET_H
