using System.Text.Json;
using RezPad.Protocol;

namespace RezPad.Host.Device;

/// <summary>
/// Knob and button numbering from the web page: asks the device (CMD <c>input_learn</c>) for the
/// next control that is turned or pressed and waits for its INPUT_LEARNED (docs/protocol.md).
/// One request at a time; a new one or <see cref="Cancel"/> ends the previous one.
/// </summary>
internal sealed class InputLearn
{
    public sealed record Result(string? Kind, int Port, string? Error);

    private readonly SerialLink _link;
    private readonly object _lock = new();
    private TaskCompletionSource<Result>? _pending;

    public InputLearn(SerialLink link)
    {
        _link = link;
        _link.PacketReceived += OnPacket;
    }

    public async Task<Result> WaitAsync(int timeoutMs, CancellationToken ct)
    {
        timeoutMs = Math.Clamp(timeoutMs, 1000, 60000);
        var tcs = new TaskCompletionSource<Result>(TaskCreationOptions.RunContinuationsAsynchronously);
        lock (_lock)
        {
            _pending?.TrySetResult(new Result(null, 0, "replaced by a newer request"));
            _pending = tcs;
        }
        var args = JsonSerializer.SerializeToElement(new { ms = timeoutMs });
        if (!_link.SendJson(MsgType.Cmd, new CmdMsg("input_learn", args)))
        {
            Finish(tcs, new Result(null, 0, "device not connected"));
            return await tcs.Task;
        }
        // The device ends it after timeoutMs (and says so); the extra second covers the link.
        using var timer = new CancellationTokenSource(timeoutMs + 1500);
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(ct, timer.Token);
        using (linked.Token.Register(() =>
        {
            if (Finish(tcs, new Result(null, 0, ct.IsCancellationRequested ? "cancelled" : null)))
            {
                SendCancel();  // the page went away: the knobs act normally again
            }
        }))
        {
            return await tcs.Task;
        }
    }

    public void Cancel()
    {
        TaskCompletionSource<Result>? p;
        lock (_lock)
        {
            p = _pending;
        }
        if (p is not null)
        {
            Finish(p, new Result(null, 0, "cancelled"));
        }
        SendCancel();
    }

    private void SendCancel()
    {
        _link.SendJson(MsgType.Cmd, new CmdMsg("input_learn", JsonSerializer.SerializeToElement(new { ms = 0 })));
    }

    private bool Finish(TaskCompletionSource<Result> tcs, Result r)
    {
        lock (_lock)
        {
            if (_pending == tcs)
            {
                _pending = null;
            }
        }
        return tcs.TrySetResult(r);
    }

    private void OnPacket(Packet packet)
    {
        if (packet.Type != MsgType.InputLearned)
        {
            return;
        }
        TaskCompletionSource<Result>? p;
        lock (_lock)
        {
            p = _pending;
        }
        if (p is null)
        {
            return;
        }
        try
        {
            using var doc = JsonDocument.Parse(packet.Body);
            var kind = doc.RootElement.TryGetProperty("kind", out var k) && k.ValueKind == JsonValueKind.String ? k.GetString() : null;
            var port = doc.RootElement.TryGetProperty("port", out var pt) && pt.TryGetInt32(out var n) ? n : 0;
            Finish(p, kind is "knob" or "button" && port > 0 ? new Result(kind, port, null) : new Result(null, 0, null));
        }
        catch (JsonException)
        {
            Finish(p, new Result(null, 0, "bad INPUT_LEARNED body"));
        }
    }
}
