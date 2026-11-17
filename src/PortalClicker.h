#pragma once

#include <QObject>
#include <QPointF>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <optional>
#include <functional>

class PortalClicker : public QObject {
    Q_OBJECT
public:
    explicit PortalClicker(QObject *parent = nullptr);
    ~PortalClicker() override;

    void prepare();
    void clickGlobal(const QPointF &globalLogicalPoint,
                     std::function<std::optional<QPointF>()> currentPointer = {});
    bool isReady() const;
    QString errorString() const;

signals:
    void readyChanged(bool ready);
    void sessionFailed(const QString &error);
    void clickFinished(bool success, const QString &error);

private slots:
    void onRequestResponse(uint response, const QVariantMap &results);
    void onSessionClosed();

private:
    struct Stream {
        uint node = 0;
        QPointF position;
        QPointF size;
        QPointF logicalSize;
        bool positioned = false;
    };
    enum class Step { Idle, Creating, SelectingDevices, SelectingSources, Starting, Ready };

    void beginSession();
    void request(const QString &interface, const QString &method,
                 const QVariantList &arguments, Step step);
    void fail(const QString &message);
    void dispatchClick(const QPointF &point,
                       const std::function<std::optional<QPointF>()> &currentPointer);
    void verifyRelativeMotion(const QPointF &point,
                              const std::function<std::optional<QPointF>()> &currentPointer,
                              const QString &session, int attemptsLeft);
    void sendButton(uint state);
    void setReady(bool ready);
    void closeSession();

    Step step_ = Step::Idle;
    QString sessionPath_;
    QString requestPath_;
    QString error_;
    QVector<Stream> streams_;
    bool ready_ = false;
    bool clickInFlight_ = false;
};
