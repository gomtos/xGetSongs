# Qt 전환 Q2c: HTTP와 LRCLIB 가사 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Kotlin `engine`의 네트워크 부분(LRCLIB 가사 조회 `LrclibLyricsProvider`, 가사 공급자 인터페이스와 `FallbackLyricsProvider`, yt-dlp 내려받기 `httpGet`)을 C++/Qt로 옮기고, 지금 단위 테스트 62개(LRCLIB 59, Fallback 3)를 같은 이름으로 통과시키며, 실제 LRCLIB 통합 테스트 4개를 통과시킨다.

**Architecture:** HTTP는 `xgs::net::HttpClient` 인터페이스(멈춤 토큰을 받는 블로킹 `get`) 뒤에 둔다. 실제 구현 `QtHttpClient`는 전용 스레드 하나가 가진 `QNetworkAccessManager`에 요청을 넘기고 부른 스레드는 답을 기다린다. LRCLIB 공급자는 이 인터페이스만 쓰므로, 로직(주소 만들기, 변형 순서, 요청 상한, JSON에서 후보 읽기, 쉬는 시간)은 **가짜 클라이언트를 양쪽에 끼워 Kotlin과 무작위 대조**하고, 시간 제한·크기 제한·취소·동시 요청 수는 루프백의 가짜 HTTP 서버로 시험한다.

**Tech Stack:** C++20, Qt 6.11.3 Core·Network·Test 정적 링크(TLS는 Schannel), Winsock(시험용 가짜 서버만), MSVC, CMake + Ninja, PowerShell 7, 대조용 JVM은 `server/build/sidecar-image`.

**Spec:** [설계 문서](../specs/2026-11-15-qt-rewrite-and-playlists-design.md) §2(가사: 설명란 → LRCLIB → 구글), §3(`qt/core`는 Qt Core·Network만 쓴다), §4(Q2 행, 대조 대상 "LRCLIB 매칭"). 앞 단계: [Q2b 보고서](../specs/2026-11-18-qt-engine-process-report.md) "Q2c에 넘길 것", [Q2a 보고서](../specs/2026-11-15-qt-engine-logic-report.md) "Q2c에 넘길 것"(후보 필드 길이 상한), [Q0 보고서](../specs/2026-11-15-qt-spike-report.md) Q8(`QNetworkAccessManager` + Schannel로 lrclib.net 호출 통과).

## 이 계획에서 작성자가 정한 것 (사용자가 바꿀 수 있다)

1. **HTTP는 `QNetworkAccessManager`로 한다**(설계 §3, Q0의 Q8). WinHTTP를 직접 부르지 않는다. 요청은 전용 스레드 하나에서 돌고 연결은 재사용된다.
2. **`find`는 블로킹 함수이고, 취소되면 값 없이 돌아온다.** Kotlin은 `CancellationException`을 던진다. C++은 `std::optional<QString>`의 빈 값을 돌려주고, 부른 쪽이 멈춤 토큰으로 "못 찾음"과 "취소"를 가린다(Q2b `YtDlpResolver::fetch`와 같은 약속).
3. **연결 제한 시간을 따로 두지 않는다.** Kotlin은 연결 10초와 요청 전체 15초 둘을 둔다. Qt에는 연결만의 제한이 없어서 요청 전체 제한(`requestTimeout`, 기본 15초) 하나만 둔다. 연결이 10~15초 걸리는 경우에만 차이가 나고, 그때도 결과("연결 문제로 그만둠, 쉬는 시간 시작")는 같으며 최대 5초 늦을 뿐이다.
4. **프록시를 쓰지 않는다**(`QNetworkProxy::NoProxy`). Kotlin의 `HttpClient`도 Windows의 프록시 설정을 읽지 않는다. 회사 프록시 뒤의 PC에서는 지금 앱처럼 LRCLIB와 yt-dlp 내려받기가 안 된다.
5. **후보 필드 길이 상한.** LRCLIB 답의 `trackName`·`artistName`·`albumName`이 UTF-16 1,000자를 넘으면 그 필드는 없는 것으로 읽는다(Q2a 인계: 괄호 제거가 깊이 × 길이로 느려진다). Kotlin에는 상한이 없다. 실제 곡 이름은 여기에 닿지 않는다.
6. **yt-dlp 내려받기의 안전장치 셋**(Kotlin `httpGet`에는 없다): 응답이 30초 동안 한 바이트도 안 오면 실패, 본문이 200MiB를 넘으면 실패, `User-Agent`는 LRCLIB와 같은 앱 이름(Kotlin은 JDK 기본값)을 보낸다. Kotlin은 멈춘 서버를 취소할 때까지 기다린다.
7. **네트워크 오류 문구는 Qt의 문구다.** `yt-dlp 다운로드에 실패했습니다: ` 뒤에 붙는 JDK 예외 문구 자리에 Qt의 오류 문구가 들어간다. `HTTP 404` 같은 상태 문구는 Kotlin과 같다.
8. **실제 서비스에 닿는 시험.** LRCLIB 통합 테스트는 `XGS_INTEGRATION=1`일 때만 돌고, 이 계획에서 **두 번**(Task 5와 관문) 돈다. 한 번에 lrclib.net 요청이 많아야 30개쯤이고, 가사 글자는 찍지 않는다(줄 수만). **yt-dlp.exe(약 18MB)를 실제로 내려받는 시험은 `XGS_INTEGRATION_DOWNLOAD=1` 뒤에 두고 이 계획에서는 돌리지 않는다**(사용자가 허락하면 따로 돌린다). `yt-dlp -U` 확인(Q2b 인계)도 같은 이유로 이 계획에 넣지 않았다.

## Global Constraints

- 정답은 Kotlin이다. `shared/`, `engine/`, `server/`, `diagnostics/`, `shell/`, `sidecar-host/`는 읽기만 한다. C++ 결과가 Kotlin과 다르면 C++을 고친다. Kotlin 쪽이 버그로 보여도 고치지 않고 보고서에 적는다. 예외는 위 "작성자가 정한 것"과 Q2a·Q2b 보고서의 "Kotlin과 다르게 한 것"뿐이다.
- **JDK(OpenJDK)와 `kotlinx.serialization`의 소스를 보지 않고, 기억에서 옮겨 쓰지 않는다.** `java.net.URLEncoder`, `java.net.http.HttpClient`, `String(bytes, UTF_8)`, `JsonPrimitive.doubleOrNull`·`booleanOrNull`의 동작은 공개 문서와 대조 도구가 관찰한 답으로만 정한다.
- 바꾸는 곳은 `qt/`(단 `qt/spike/` 제외), `THIRD-PARTY-NOTICES.md`(필요할 때만), Task 6의 보고서뿐이다.
- `xgscore`는 Qt Core, Qt Network, Win32 API만 쓴다. 네임스페이스 `xgs`, C++20, `/W4 /WX /permissive- /utf-8`. 금지 호출 목록은 그대로다(`qt/tools/forbidden-calls.ps1`).
- **단위 테스트와 `ctest`는 루프백(`127.0.0.1`) 밖으로 아무것도 보내지 않는다**(DNS 조회 포함: 없는 호스트 이름을 실제로 찾아보는 시험을 쓰지 않는다). 실제 서비스는 `XGS_INTEGRATION=1`인 통합 테스트만 부른다.
- **가사는 저작물이다.** 실제 LRCLIB가 준 가사의 글자를 출력·로그·보고서·커밋에 한 글자도 남기지 않는다(줄 수, 글자 수, 키 이름만). 시험의 가사는 Kotlin 시험처럼 지어낸 것만 쓴다.
- 테스트는 Qt Test. Kotlin의 `@Test` 하나가 같은 이름의 슬롯 하나가 되고 단언을 하나도 빼지 않는다. 기대값·통계 문턱·생성기의 강도를 바꿔 통과시키지 않는다. 테스트는 창을 띄우지 않는다.
- 대조는 종류마다 1만 건 × seed 셋과 10만 건 한 번을 돈다(`-DebugTool` 한 번 포함). 이식 Task마다 변이가 대조나 시험에 잡히는지 본다.
- 관문은 릴리스와 디버그 두 빌드에서 `ctest`를 돈다. Gradle은 돌리지 않는다. 실행 중인 xGetSongs와 `%APPDATA%\xGetSongs`를 건드리지 않는다. 임시 파일은 `QTemporaryDir` 아래에만 만든다.
- 커밋은 `main`에 직접, 메시지는 한국어, 끝에 실행하는 세션의 Co-Authored-By 줄. push하지 않는다.

## Review Focus

Kotlin 테스트 62개가 다루지 않지만 실제로 만날 것이다.

1. **정적 빌드에 TLS가 안 들어간다.** 루프백 시험은 모두 `http://`라서, Schannel 플러그인이 빠져도 전부 통과하고 실제 `https://lrclib.net`만 실패한다 → Task 1의 `theSchannelTlsBackendIsLinkedIn`(네트워크 없이 `QSslSocket::supportsSsl()`과 활성 백엔드 이름을 본다).
2. **Qt가 주소를 다시 쓴다.** `QUrl`이 `%7E`를 풀거나 `*`·`'`·`(`를 다르게 적으면 Kotlin과 다른 주소가 나간다 → Task 1의 `theQueryStringReachesTheServerByteForByte`와 Task 4의 대조(나간 주소 비교).
3. **곡 이름에 짝 없는 서로게이트, NUL, `&`·`=`·`#`·`+`·`%`가 있다**(유튜브 제목에서 온다) → Task 4 대조의 주소 통계.
4. **LRCLIB 답의 형이 다르다.** `duration`이 `"258"`(문자열)·`NaN`·`1e400`·`0x10`, `instrumental`이 `"true"`·`TRUE`·`1`, 이름이 숫자·`null`·객체 → Task 4 대조의 "형이 틀린 필드" 통계.
5. **여러 곡이 한꺼번에 가사를 찾다가 한꺼번에 취소된다**(Q2d의 모습). 자리(동시 2개)를 기다리던 호출도 곧 돌아와야 하고, 클라이언트 스레드에 요청이 남지 않아야 한다 → Task 1의 `cancellingManyCallsAtOnceReturnsThemAllPromptly`, Task 3의 `aCallWaitingForASlotReturnsWhenItIsCancelled`.
6. **서버가 응답 중간에 연결을 끊거나 길이를 속인다** → Task 1의 `aConnectionClosedInTheMiddleOfTheBodyIsAFailureNotATimeout`.

---

## File Structure

| 파일 | 하는 일 |
|---|---|
| `qt/core/net/http_client.h` | `HttpClient` 인터페이스, `HttpRequest`, `HttpResponse` |
| `qt/core/net/qt_http_client.h/.cpp` | `QNetworkAccessManager`를 가진 전용 스레드 |
| `qt/core/net/http_fetch.h/.cpp` | `tools::Fetch`의 실제 구현 (Kotlin `httpGet`) |
| `qt/core/lyrics/lyrics_provider.h/.cpp` | `LyricsProvider`, `NoLyricsProvider`, `FallbackLyricsProvider` |
| `qt/core/lyrics/lrclib_lyrics_provider.h/.cpp` | `LrclibLyricsProvider.kt` |
| `qt/tests/support/fake_http_server.h/.cpp` | 루프백 HTTP/1.1 서버(Winsock, 연결마다 스레드) |
| `qt/tests/support/fake_http_client.h/.cpp` | 대본대로 답하는 `HttpClient`, 요청을 세는 감싸개 |
| `qt/tests/support/fake_lyrics_provider.h` | Kotlin `FakeLyricsProvider` |
| `qt/tests/tst_qt_http_client.cpp`, `tst_http_fetch.cpp` | 새 시험(Kotlin에 짝 없음) |
| `qt/tests/tst_lrclib_lyrics_provider.cpp`, `tst_fallback_lyrics_provider.cpp` | Kotlin 테스트 클래스마다 하나 |
| `qt/tests/integration/tst_real_lrclib.cpp` | `RealLrclibIntegrationTest.kt`와 내려받기 시험 |
| `qt/tools/differential/*` | 종류 `W`에 op `lf`(LRCLIB 조회) |
| `docs/superpowers/specs/2026-11-20-qt-engine-lrclib-report.md` | 결과 보고서 |

**대조 op `W lf`** (종류 글자 26개를 다 써서 `W`에 op를 더한다)

| 사례 | 답 | 통계 문턱 (1만 건 기준) |
|---|---|---|
| 질의(가수, 제목, 앨범, 길이), `maxRequests`, 요청 차례대로의 대본(상태 코드 + 본문 바이트 / 연결 실패 / 시간 초과 / 그 밖의 IO 오류 / 2MiB 넘는 본문) | 나간 주소 목록(hex), 결과(가사 hex 또는 `NULL`), 같은 공급자의 **두 번째 호출이 낸 요청 수**(쉬는 시간이 시작됐는지) | 가사 찾음 15% 이상, 못 찾음 30% 이상, `/search`까지 감 40% 이상, 쉬는 시간 시작 10% 이상, 상한에 닿음 5% 이상, 주소에 ASCII 밖 글자 20% 이상, 주소에 `&`·`=`·`#`·`+`·`%`·공백 중 하나 10% 이상, 짝 없는 서로게이트 2% 이상, 형이 틀린 필드가 든 레코드 10% 이상, JSON이 아닌 본문 5% 이상, 후보 둘 이상인 검색 답 15% 이상 |

Kotlin 쪽은 `java.net.http.HttpClient`(공개 추상 클래스)를 상속한 가짜를 `LrclibLyricsProvider(client = …)`에 넣는다. 이름 필드는 1,000자 아래로만 만든다(작성자가 정한 것 5는 따로 시험).

---

### Task 1: HTTP 클라이언트와 가짜 서버

**Files:**
- Create: `qt/core/net/http_client.h`, `qt/core/net/qt_http_client.h`, `qt/core/net/qt_http_client.cpp`
- Create: `qt/tests/support/fake_http_server.h`, `qt/tests/support/fake_http_server.cpp`, `qt/tests/tst_qt_http_client.cpp`
- Modify: `qt/CMakeLists.txt`(Qt6 `Network` 찾기), `qt/core/CMakeLists.txt`(`Qt6::Network` 링크), `qt/tests/CMakeLists.txt`, `qt/tests/support/CMakeLists.txt`, `qt/tools/test-classes.json`

**Interfaces:**
- Consumes: 없음
- Produces:

```cpp
namespace xgs::net {
struct HttpRequest {
    QString url;                                        // sent as written; not re-encoded
    QList<std::pair<QByteArray, QByteArray>> headers;
    std::chrono::milliseconds totalTimeout{0};          // from sending until the body is in; 0: none
    std::chrono::milliseconds idleTimeout{0};           // no byte for this long; 0: none
    qint64 maxBodyBytes = -1;                           // -1: no limit
};
enum class HttpStatus { Response, Unreachable, Failed, Cancelled };
struct HttpResponse {
    HttpStatus status = HttpStatus::Failed;
    int code = 0;                 // Response only
    QByteArray body;              // Response only; empty when bodyTooLarge
    bool bodyTooLarge = false;    // the body was longer than maxBodyBytes: reading stopped, the connection was dropped
    QString error;                // Unreachable / Failed: text for a person
};
class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse get(const HttpRequest& request, std::stop_token stop) = 0;   // blocking, any thread, never throws
};
class QtHttpClient final : public HttpClient { /* QCoreApplication must exist; destructor aborts what is in flight */ };
}
namespace xgs::testing {
class FakeHttpServer {   // 127.0.0.1, port chosen by the system; one thread per connection; "Connection: close"
public:
    struct Seen { QByteArray method, path, rawQuery, userAgent, accept; };
    class Exchange {
    public:
        const Seen& request() const;
        void reply(int status, const QByteArray& body, const QByteArray& contentType = "application/json; charset=utf-8");
        void sendHeaders(int status, bool chunked, qint64 contentLength = -1);
        bool write(const QByteArray& bytes);   // false: the client went away
        void closeAbruptly();
    };
    using Handler = std::function<void(Exchange&)>;
    void route(const QByteArray& path, Handler handler);   // others: 404
    QString base() const;                                   // "http://127.0.0.1:<port>"
    QList<Seen> seen() const; void clearSeen();
    void stop();                                            // the port then refuses connections
};
}
```

`Unreachable`은 연결 거절, 호스트를 못 찾음, 네트워크에 닿지 못함, 시간 초과(`totalTimeout`·`idleTimeout`)다. `Failed`는 그 밖의 전부(잘못된 주소, `http`·`https`가 아닌 스킴, TLS 실패, 본문 중간에 끊김, 리다이렉트 한도). 리다이렉트는 `https`에서 `http`로 가는 것만 빼고 따른다(Kotlin `Redirect.NORMAL`; Qt의 `NoLessSafeRedirectPolicy`). 프록시는 쓰지 않는다. 본문이 `maxBodyBytes`를 넘으면 더 읽지 않고 요청을 끊는다(메모리에 쌓지 않는다).

- [ ] **Step 1: `ctest`가 Qt Test 출력을 남기게 한다** (Q2b의 열린 문제: 실패한 `ctest`에 출력이 없었다). `xgs_add_test`가 등록하는 명령을 `<name> -o <빌드 폴더>/qttest/<name>.txt,txt -o -,txt`로 바꾼다. 확인: `ctest -R tst_smoke` 뒤 `build/qt/qttest/tst_smoke.txt`에 `Totals:` 줄이 있다.
- [ ] **Step 2: 가짜 서버와 그 시험 슬롯 둘** — `theFakeServerAnswersARawSocketRequest`(Winsock으로 직접 `GET /x?a=%20b HTTP/1.1`을 보내 상태 줄과 본문, `seen()`의 `rawQuery == "a=%20b"`를 본다), `aStoppedFakeServerRefusesConnections`.
- [ ] **Step 3: 실패하는 시험을 쓴다** (`tst_qt_http_client.cpp`, 모두 새 이름이라 `test-classes.json`의 `added`에 넣는다)

| 슬롯 | 단언 |
|---|---|
| `aGetReturnsTheStatusAndTheBody` | 200과 한글 본문 바이트 그대로; 404도 `Response`이고 본문이 온다 |
| `theHeadersOfTheRequestAreSent` | `User-Agent`, `Accept`가 준 값 그대로; 메서드 `GET` |
| `theQueryStringReachesTheServerByteForByte` | `?q=%7E%2A%27%28%29*~.-_%20%2B+%ED%95%9C&x=` 를 보내면 `rawQuery`가 한 바이트도 다르지 않다 |
| `aRedirectIsFollowed` | 302 → 다른 경로의 200 본문; `seen()`에 둘 다 있다 |
| `aBodyOfExactlyTheLimitIsReadAndOneByteMoreIsTooLarge` | `maxBodyBytes = 1000`: 1000바이트는 본문, 1001바이트는 `bodyTooLarge`이고 `body`가 비었다 |
| `aBodyThatNeverEndsIsDroppedAtTheLimit` | 청크로 32MiB를 쓰는 서버: `bodyTooLarge`, 서버의 `write`가 15초 안에 false를 보고 쓴 양이 32MiB보다 적다 |
| `aSlowAnswerIsUnreachableAtTheTotalTimeout` | 서버가 5초 잔다, `totalTimeout` 1초: `Unreachable`, 4초 안에 돌아온다 |
| `aBodyThatStallsIsUnreachableAtTheTotalTimeout` | 헤더와 한 바이트 뒤 5초 잔다: 위와 같다 |
| `aBodyThatStallsIsUnreachableAtTheIdleTimeout` | `idleTimeout` 1초, `totalTimeout` 없음: `Unreachable`, 4초 안 |
| `aRefusedConnectionIsUnreachable` | 멈춘 서버의 주소: `Unreachable`, 10초 안, `error`가 비지 않았다 |
| `aConnectionClosedInTheMiddleOfTheBodyIsAFailureNotATimeout` | `Content-Length: 100`을 알리고 10바이트 뒤 `closeAbruptly()`: `Failed` |
| `aBrokenOrUnsupportedUrlIsAFailureWithoutARequest` | `http://[not a url`, 빈 글, `ftp://127.0.0.1/api`: 모두 `Failed`, `seen()`이 비었다 |
| `stoppingWhileTheServerSleepsReturnsPromptly` | 서버가 10초 잔다: 멈춤 요청 뒤 3초 안에 `Cancelled` |
| `aStopRequestedBeforeTheCallSendsNothing` | `Cancelled`, `seen()`이 비었다 |
| `cancellingManyCallsAtOnceReturnsThemAllPromptly` | 스레드 16개가 자는 서버를 부르다 한 토큰으로 멈춘다: 모두 3초 안에 `Cancelled`; 그 뒤 같은 클라이언트의 새 요청이 200을 받는다 |
| `callsFromManyThreadsAllGetTheirOwnAnswer` | 스레드 8개 × 요청 25개, 경로마다 다른 본문: 전부 제 본문을 받는다 |
| `destroyingTheClientWithACallInFlightEndsTheCall` | 다른 스레드의 `get`이 `Failed` 또는 `Cancelled`로 5초 안에 돌아오고 소멸자가 끝난다 |
| `theSchannelTlsBackendIsLinkedIn` | `QSslSocket::supportsSsl()`이 참, `QSslSocket::activeBackend() == "schannel"` (네트워크를 쓰지 않는다) |

- [ ] **Step 4: 돌려서 실패를 본다** — `cmake --build build/qt && ctest --test-dir build/qt -R tst_qt_http_client --output-on-failure`, 기대: 컴파일 실패(`QtHttpClient` 없음).
- [ ] **Step 5: `QtHttpClient`를 만든다.** 전용 `QThread` 하나에 `QNetworkAccessManager`(프록시 없음, 리다이렉트 정책은 위)를 두고, `get`은 요청을 그 스레드에 넘긴 뒤 답을 기다린다. 멈춤은 `std::stop_callback`이 그 스레드에 "이 요청을 끊어라"를 보낸다. 본문은 `readyRead`마다 읽어 한도를 넘는 순간 끊는다. 정적 빌드에서 Schannel 플러그인이 들어가게 하는 방법은 `qt/spike/CMakeLists.txt`가 한 방식을 따른다.
- [ ] **Step 6: 통과를 본다** — 같은 명령, 기대: 20개 슬롯 통과. 디버그 빌드에서도 한 번. 스위트를 연달아 10번 돌려 모두 통과하는지 본다(시간에 기대는 시험이 있다).
- [ ] **Step 7: 변이** — (a) 한도 검사를 `>=`로, (b) 멈춤 콜백을 없앰, (c) 리다이렉트 정책을 끔: 각각 어느 슬롯이 실패하는지 보고 되돌린다.
- [ ] **Step 8: 커밋** — `git add qt && git commit -m "Qt: HTTP 클라이언트와 시험용 가짜 서버"`

### Task 2: yt-dlp 내려받기 (`Fetch`의 실제 구현)

**Files:**
- Create: `qt/core/net/http_fetch.h`, `qt/core/net/http_fetch.cpp`, `qt/tests/tst_http_fetch.cpp`
- Modify: `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/tools/test-classes.json`

**Interfaces:**
- Consumes: `net::HttpClient`, `net::HttpRequest`(Task 1); `tools::Fetch`, `tools::ToolManager`, `xgs::Outcome`(Q2b); `xgs::testing::FakeProcessRunner`, `FakeHttpServer`
- Produces:

```cpp
namespace xgs::net {
inline constexpr qint64 FetchMaxBytes = 200LL * 1024 * 1024;
inline constexpr std::chrono::milliseconds FetchIdleTimeout{30000};
/// Kotlin httpGet: GET url, redirects followed. Ok with the body when the status is 200; Failed "HTTP <code>" otherwise;
/// Failed with the client's text when it could not be reached or failed; Cancelled when stop was requested.
tools::Fetch httpFetch(HttpClient& client, std::chrono::milliseconds idleTimeout = FetchIdleTimeout, qint64 maxBytes = FetchMaxBytes);
}
```

본문이 `maxBytes`를 넘으면 `Failed`이고 문구는 `내려받는 파일이 너무 큽니다.`다(새 한국어 문구, 사용자가 바꿀 수 있다). `User-Agent`는 `lyrics::DefaultUserAgent`와 같은 글(`xGetSongs/1.0 (https://github.com/gomtos/xGetSongs)`)이고, Task 3보다 먼저 만들어지므로 상수는 `qt/core/net/http_client.h`에 `xgs::net::AppUserAgent`로 두고 Task 3이 그것을 쓴다.

- [ ] **Step 1: 실패하는 시험** (`tst_http_fetch.cpp`, 모두 `added`)

| 슬롯 | 단언 |
|---|---|
| `aSuccessfulDownloadReturnsTheBytes` | 서버가 0~255가 섞인 3MiB를 준다: `Ok`, 바이트가 같다 |
| `anyStatusButOkIsAFailureThatNamesTheStatus` | 404, 500, 204 각각: `Failed`, `error == "HTTP 404"` 등 |
| `aRedirectToTheFileIsFollowed` | 302 → 200(GitHub의 `latest/download`가 이렇게 답한다): `Ok` |
| `aRefusedConnectionIsAFailureWithAText` | `Failed`, `error`가 비지 않았다 |
| `aDownloadThatStallsFailsAtTheIdleTimeout` | `idleTimeout` 1초: `Failed`, 4초 안 |
| `aBodyLargerThanTheLimitIsAFailure` | `maxBytes` 1000, 본문 1001: `Failed`, `error == "내려받는 파일이 너무 큽니다."` |
| `stoppingTheDownloadIsACancel` | 자는 서버, 멈춤: `Cancelled`, 3초 안 |
| `theAppUserAgentIsSent` | `seen()`의 `userAgent`가 `AppUserAgent` |
| `installYtDlpDownloadsThroughTheFetchAndInstalls` | `ToolManager`(가짜 실행기가 `--version`에 `2026.01.01`을 찍는다, `binDir`은 `QTemporaryDir`) + `httpFetch` + 1,000,000바이트를 주는 가짜 서버: `Ok`, `yt-dlp.exe`의 바이트가 서버가 준 것과 같고 `yt-dlp.new.exe`가 없다 |
| `installYtDlpReportsAFailedDownloadWithTheKoreanPrefix` | 서버 404: `Failed`, `error == "yt-dlp 다운로드에 실패했습니다: HTTP 404"` |

- [ ] **Step 2: 실패를 본다** — `ctest --test-dir build/qt -R tst_http_fetch --output-on-failure`, 기대: 컴파일 실패.
- [ ] **Step 3: `httpFetch`를 만든다.**
- [ ] **Step 4: 통과를 본다** — 10개 슬롯, 두 빌드.
- [ ] **Step 5: 커밋** — `git commit -m "Qt: yt-dlp 내려받기를 HTTP 클라이언트에 잇기"`

### Task 3: 가사 공급자와 LRCLIB

**Files:**
- Create: `qt/core/lyrics/lyrics_provider.h`, `lyrics_provider.cpp`, `lrclib_lyrics_provider.h`, `lrclib_lyrics_provider.cpp`
- Create: `qt/tests/support/fake_lyrics_provider.h`, `fake_http_client.h`, `fake_http_client.cpp`
- Create: `qt/tests/tst_fallback_lyrics_provider.cpp`, `qt/tests/tst_lrclib_lyrics_provider.cpp`
- Modify: `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/tests/support/CMakeLists.txt`, `qt/tools/test-classes.json`
- Kotlin 원본(읽기만): `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/{LyricsProvider,FallbackLyricsProvider,LrclibLyricsProvider}.kt`, `engine/src/test/kotlin/com/xgetsongs/engine/lyrics/{FallbackLyricsProviderTest,LrclibLyricsProviderTest}.kt`, `engine/src/test/kotlin/com/xgetsongs/engine/testutil/Fakes.kt`(`FakeLyricsProvider`)

**Interfaces:**
- Consumes: `net::HttpClient`(Task 1), `net::AppUserAgent`(Task 2), `xgs::LyricsQuery`(`model/models.h`), `lyrics::LyricsCandidate`·`isMatch`·`pick`·`titleVariants`·`artistVariants`(`lyrics_matcher.h`), `lyrics::tidy`(`lyrics_extractor.h`), `xgs::json::parse`·`find`, `kt::toDoubleOrNull`, Q2b 줄 나누기의 UTF-8 해석(JVM처럼 잘못된 바이트를 U+FFFD로; 비공개면 함수로 꺼낸다)
- Produces:

```cpp
namespace xgs::lyrics {
class LyricsProvider {
public:
    virtual ~LyricsProvider() = default;
    /// The tidied lyrics, or nothing when none were found, the lookup did not work, or stop was requested. Never throws.
    virtual std::optional<QString> find(const LyricsQuery& query, std::stop_token stop) = 0;
};
class NoLyricsProvider final : public LyricsProvider { /* always nothing */ };
class FallbackLyricsProvider final : public LyricsProvider {
public:
    explicit FallbackLyricsProvider(std::vector<LyricsProvider*> providers);   // referenced, not owned; asked in order
};

inline constexpr QStringView DefaultBaseUrl = u"https://lrclib.net/api";
struct LrclibOptions {
    QString baseUrl = DefaultBaseUrl.toString();
    QString userAgent = net::AppUserAgent.toString();
    std::chrono::milliseconds requestTimeout{15000};
    int maxRequests = 8;
    int maxConcurrent = 2;                              // below 1 counts as 1
    std::chrono::milliseconds coolDown{300000};
    std::function<std::chrono::nanoseconds()> clock;    // empty: std::chrono::steady_clock
};
inline constexpr qint64 LrclibMaxBodyBytes = 2 * 1024 * 1024;
inline constexpr qsizetype LrclibMaxNameUnits = 1000;
class LrclibLyricsProvider final : public LyricsProvider {
public:
    LrclibLyricsProvider(net::HttpClient& client, LrclibOptions options = {});   // client referenced, must outlive
};
}
namespace xgs::testing {
class FakeLyricsProvider;          // Kotlin FakeLyricsProvider: answers with a function, records the queries
class CountingHttpClient;          // wraps a client, counts get() calls
class FakeHttpClient;              // answers get() from a script, records the URLs (used by Task 4)
}
```

동작은 Kotlin 파일의 문서 주석과 코드가 정한다. 옮길 때 지킬 대응:

| Kotlin | C++ |
|---|---|
| `Outcome.Body` | `Response`, 200, `bodyTooLarge` 아님 |
| `Outcome.Miss` | 200인데 `bodyTooLarge`; 그 밖의 상태 코드; `Failed` |
| `Outcome.Stop` + 쉬는 시간 | 429·502·503·504(본문 크기와 무관), `Unreachable` |
| `Outcome.Stop`, 쉬는 시간 없음 | 요청 상한에 닿음; 자리를 얻고 보니 쉬는 중 |
| 취소 | `Cancelled`이거나 자리를 기다리다 멈춤 요청: 빈 값, **쉬는 시간을 시작하지 않는다** |
| `Semaphore(maxConcurrent)` | 멈춤 토큰으로 깨울 수 있는 세는 자리(뮤텍스 + `condition_variable_any`) |
| `URLEncoder.encode(v, UTF_8).replace("+", "%20")` | 직접 쓴 인코더(규칙은 Java 문서의 `URLEncoder`; 문서가 말하지 않는 것, 특히 짝 없는 서로게이트는 Task 4 대조가 정한다) |
| `doubleOrNull`, `booleanOrNull` | `kt::toDoubleOrNull`; 불리언 규칙은 Task 4 대조가 정한다(이 Task는 `true`·`false`만) |
| `client`가 null이면 만든다 | 항상 받는다 |

- [ ] **Step 1: Fallback 시험 3개와 `FakeLyricsProvider`** — `FallbackLyricsProviderTest.kt`의 세 `@Test`를 같은 이름으로. 실패를 보고(`컴파일 실패`), `lyrics_provider.h/.cpp`를 만들고, 통과를 본다(3개).
- [ ] **Step 2: LRCLIB 시험 59개를 쓴다** — `LrclibLyricsProviderTest.kt`의 `@Test` 59개를 같은 이름·같은 단언으로, `FakeHttpServer` + `QtHttpClient`에 대고. 고쳐 옮기는 것:
  - `blocking { }` → 그냥 부른다. `async`/`launch` → `std::jthread`. `TestTimeSource` → 시험이 움직이는 `clock`.
  - `cancellingTheCallWhileTheServerSleepsReturnsPromptlyWithACancellationException`: 3초 안에 돌아오고, 값이 비었고, 토큰에 멈춤이 요청돼 있다.
  - `aGivenHttpClientIsUsed`: `CountingHttpClient`로 감싼 클라이언트를 주고 가사가 오며 센 수가 0보다 크다(프록시 선택기·실행기 단언의 대응).
  - `aRefusedConnectionStartsTheCoolDown`: 프록시 선택기가 세던 요청 수를 `CountingHttpClient`가 센다(1, 1, 2).
- [ ] **Step 3: 더하는 시험** (`added`)

| 슬롯 | 단언 |
|---|---|
| `aCallWaitingForASlotReturnsWhenItIsCancelled` | `maxConcurrent` 1, 첫 호출이 자는 서버에 걸려 있다. 둘째 호출을 멈추면 1초 안에 빈 값으로 돌아오고 서버는 요청을 하나만 봤다 |
| `aCancelledCallDoesNotStartTheCoolDown` | 취소된 호출 뒤 같은 공급자의 다음 호출이 요청을 보낸다 |
| `aNameLongerThanAThousandUnitsIsReadAsMissing` | `trackName`이 1,001자인 레코드는 답이 아니고, 1,000자인(질의와 같은) 레코드는 답이다 |
| `malformedUtf8InABodyIsReadLikeKotlinDoes` | 가사에 `0xFF` 한 바이트가 든 200 답: 가사의 그 자리가 U+FFFD |
| `manyCallsCancelledAtOnceAllReturn` | 스레드 12개가 `maxConcurrent` 2로 자는 서버를 부르다 한꺼번에 멈춘다: 모두 3초 안에 돌아온다 |

- [ ] **Step 4: 실패를 본다** — `ctest --test-dir build/qt -R "tst_lrclib|tst_fallback" --output-on-failure`, 기대: 컴파일 실패(`LrclibLyricsProvider` 없음).
- [ ] **Step 5: `LrclibLyricsProvider`를 만든다.**
- [ ] **Step 6: 통과를 본다** — 64개 슬롯(59 + 5), 두 빌드, `pwsh qt/tools/test-names.ps1`이 두 클래스에서 빠진 이름 0. 스위트를 연달아 10번.
- [ ] **Step 7: 변이** — (a) 503을 Miss로, (b) `/search`의 매개변수 순서를 바꿈, (c) 취소 때 쉬는 시간을 시작, (d) 자리를 돌려주지 않음(취소 경로): 각각 잡는 슬롯을 적고 되돌린다.
- [ ] **Step 8: 커밋** — `git commit -m "Qt: 가사 공급자와 LRCLIB 조회 이식"`

### Task 4: LRCLIB 조회를 Kotlin과 대조

**Files:**
- Modify: `qt/tools/differential/Oracle.java`, `gen.cpp`, `main.cpp`, `run.ps1`
- Modify(대조가 버그를 찾으면): `qt/core/lyrics/lrclib_lyrics_provider.cpp`, `qt/tests/tst_lrclib_lyrics_provider.cpp`

**Interfaces:**
- Consumes: `LrclibLyricsProvider`, `xgs::testing::FakeHttpClient`(Task 3)
- Produces: 대조 op `W lf`(위 표의 사례·답·문턱)

- [ ] **Step 1: 생성기** — `gen.cpp`에 `W lf` 사례. 질의: Q2a의 제목·가수 생성기(한글, 괄호, 크레딧, 따옴표)에 `& = # + % ~ * ' ( )`, 공백, NUL, 짝 없는 서로게이트를 섞는다. 대본: 요청마다 200 + 레코드/배열(맞는 곡, 다른 곡, 길이 틀린 곡, 가사 짧은 곡, 연주곡, 형이 틀린 필드: `duration`에 `"258"`·`NaN`·`1e400`·`0x10`·`258.4`, `instrumental`에 `"true"`·`TRUE`·`1`·`null`, 이름에 숫자·`null`·객체·배열), JSON이 아닌 본문, 빈 배열, 404·500·429·502·503·504, 연결 실패, 시간 초과, 그 밖의 IO 오류, 2MiB 넘는 본문(양쪽 모두 "너무 큼" 표시로 넘기고 실제 바이트를 만들지 않는다: Kotlin 쪽 가짜는 2MiB + 1바이트를 내는 스트림을 준다).
- [ ] **Step 2: Kotlin 쪽** — `Oracle.java`에 `java.net.http.HttpClient`를 상속한 가짜(공개 API만: `send`가 대본의 다음 답을 `HttpResponse<InputStream>`으로 주거나 `ConnectException`·`HttpTimeoutException`·`IOException`을 던지고, 요청 주소를 적는다). `LrclibLyricsProvider(client, baseUrl = "http://h/api", timeSource = 고정)`로 `find`를 두 번 부른다(둘째는 요청 수만 센다).
- [ ] **Step 3: C++ 쪽** — `main.cpp`에 같은 대본을 `FakeHttpClient`로.
- [ ] **Step 4: 통계와 문턱** — `run.ps1`에 위 표의 문턱. 문턱에 못 미치면 **생성기의 섞는 비율을 고친다**(문턱은 내리지 않는다).
- [ ] **Step 5: 돌린다** — `pwsh qt/tools/differential/run.ps1 -Kinds W -PerKind 10000 -Seed 20261115`, 기대: 불일치 0, 문턱 충족. 불일치가 나오면 C++을 고치고, 고친 것마다 `tst_lrclib_lyrics_provider.cpp`에 그 입력의 시험을 더한다(`added`). seed `1`, `987654321`, 10만 건 seed `424242`도.
- [ ] **Step 6: 변이** — (a) 인코더가 `~`를 그대로 둠, (b) `booleanOrNull`이 대소문자를 무시, (c) `pick` 대신 첫 후보, (d) `/get` 답에 `tidy`를 빼먹음: 각각 대조가 불일치를 내는지 보고 되돌린다.
- [ ] **Step 7: 관찰 표를 적어 둔다** — `URLEncoder`가 그대로 두는 글자, 짝 없는 서로게이트의 결과, `booleanOrNull`·`doubleOrNull`이 받는 글. Task 6 보고서에 들어간다.
- [ ] **Step 8: 커밋** — `git commit -m "Qt: LRCLIB 조회를 Kotlin과 대조"`

### Task 5: 실제 LRCLIB 통합 테스트

**Files:**
- Create: `qt/tests/integration/tst_real_lrclib.cpp`
- Modify: `qt/tests/integration/CMakeLists.txt`, `qt/tools/test-classes.json`, `qt/README.md`
- Kotlin 원본(읽기만): `engine/src/test/kotlin/com/xgetsongs/engine/integration/RealLrclibIntegrationTest.kt`

**Interfaces:**
- Consumes: `QtHttpClient`, `LrclibLyricsProvider`, `httpFetch`, `tools::YtDlpUrl`

- [ ] **Step 1: 시험을 쓴다** — Kotlin의 `@Test` 4개(`findsTheLyricsOfAKnownSong`, `aTitleWithAParentheticalCreditStillFindsTheSong`, `aMadeUpSongIsNotFound`, `theAnswersOfTheServiceHaveTheShapeTheProviderReads`)를 같은 이름·같은 단언으로. `XGS_INTEGRATION=1`이 아니면 `QSKIP`. Kotlin의 `assumeTrue(serviceAnswers())`처럼 서비스가 세 번(2초 간격) 답하지 않으면 실패가 아니라 `QSKIP`. **출력은 구조만**: 상태 코드, 키 이름과 값의 종류, 줄 수·글자 수. 가사 글자, 곡 이름 값은 찍지 않는다.
- [ ] **Step 2: 더하는 슬롯** (`added`)
  - `theRealServiceIsReachedOverSchannelTls`: 위 조회가 쓴 클라이언트로 `https://lrclib.net/api/search?track_name=zz-no-such-song-xgetsongs-0000`의 상태가 200~599(실제 TLS 연결이 됐다).
  - `downloadsTheRealYtDlpExecutable`: **`XGS_INTEGRATION_DOWNLOAD=1`일 때만**(아니면 `QSKIP`). `httpFetch`로 `tools::YtDlpUrl`을 받아 `Ok`, 1,000,000바이트 이상, 처음 두 바이트가 `MZ`. 디스크에 쓰지 않고 실행하지 않는다. **이 계획에서는 이 변수를 켜지 않는다.**
- [ ] **Step 3: 한 번 돌린다** — 릴리스 빌드, `XGS_INTEGRATION=1`만 켜고 `build/qt/tst_real_lrclib.exe -o <파일>,txt`. 기대: 5개 통과(또는 서비스가 없으면 건너뜀 — 그 경우 보고서에 "확인하지 못함"으로 적는다), 1개 건너뜀(내려받기). 출력 파일에 가사 글자가 없는지 눈으로 확인한다.
- [ ] **Step 4: README** — `qt/README.md`의 "실제 도구 통합 테스트"에 `tst_real_lrclib`, 두 환경 변수, "가사를 찍지 않는다"를 더한다.
- [ ] **Step 5: 커밋** — `git commit -m "Qt: 실제 LRCLIB 통합 테스트 이식"`

### Task 6: 관문과 보고서

**Files:**
- Create: `docs/superpowers/specs/2026-11-20-qt-engine-lrclib-report.md`
- Modify: `qt/README.md`(관문 절이 틀려진 곳만), `THIRD-PARTY-NOTICES.md`(Qt Network가 더해져 적을 것이 생기면)

- [ ] **Step 1: 관문을 처음부터 돌린다** (원본 출력은 스크래치 폴더에 파일로)
  - 우리 대상을 깨끗이 다시 빌드, 릴리스와 디버그 (`vcpkg_installed`는 지우지 않는다): 경고 0.
  - `ctest --test-dir build/qt --output-on-failure`, `ctest --test-dir build/qt-debug --output-on-failure`: 모두 통과. **실패가 하나라도 있으면 `qttest/<이름>.txt`를 보관하고 원인을 찾는다**(Q2b의 `tst_system_process_runner` 실패가 다시 나오면 그 출력이 이번에는 남는다 — 보고서에 그대로 적는다).
  - `pwsh qt/tools/test-names.ps1`: 빠진 이름 0. `pwsh qt/tools/forbidden-calls.ps1`: 0.
  - `pwsh qt/tools/differential/run.ps1`: 모든 종류, 1만 건 × seed `20261115`·`1`·`987654321`, `-DebugTool` 한 번, 10만 건 seed `424242`: 불일치 0, 문턱 충족.
  - 통합 테스트, 릴리스, 한 번: `XGS_INTEGRATION=1`로 `tst_real_ffmpeg_tagging`, `tst_real_lrclib`. `tst_real_ytdlp`는 **돌리지 않는다**(이 단계는 프로세스 실행기와 yt-dlp 코드를 바꾸지 않는다; 바꿨다면 한 번 돌린다). `XGS_INTEGRATION_AUDIO`, `XGS_INTEGRATION_DOWNLOAD`는 켜지 않는다.
- [ ] **Step 2: 보고서** — Q2b 보고서와 같은 모양: 요약, 관문 표(숫자마다 원본 파일), 관찰 표(`URLEncoder`, 불리언·숫자 읽기, Qt 네트워크 오류가 `Unreachable`/`Failed` 중 어디로 갔는지), 대조가 찾은 C++ 버그, 변이 결과, **Kotlin과 다르게 한 것**(위 "작성자가 정한 것" 여덟과 실행 중 더해진 것, 각각 예 하나), 고치지 않은 Kotlin 쪽 동작, Review Focus 여섯 가지가 어떻게 됐나, 확인하지 못한 것(HTTPS의 리다이렉트·시간 제한은 루프백 `http`로만 봄, 프록시 환경, Windows 10, 실제 yt-dlp 내려받기, `yt-dlp -U`), 실제 서비스에 보낸 요청 수, Q2d에 넘길 것(가사 공급자를 작업에 잇기, 미룬 통합 시험 `downloadsAShortVideoAsM4aWithoutReencoding`, 클라이언트 하나를 앱 전체가 나눠 쓰기), 사용자가 정할 것. 측정하지 않은 것을 사실처럼 쓰지 않는다.
- [ ] **Step 3: 커밋** — `git commit -m "문서: Qt 전환 Q2c(HTTP·LRCLIB) 보고서"`
