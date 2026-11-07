using RezPad.Protocol;

namespace RezPad.Host.Device;

/// <summary>Latest SENSOR reading from the device's room sensor (for the web page).</summary>
internal sealed class RoomSensor
{
    private (SensorMsg Reading, DateTimeOffset At)? _latest;

    public (SensorMsg Reading, DateTimeOffset At)? Latest => _latest;

    public void Update(SensorMsg reading) => _latest = (reading, DateTimeOffset.UtcNow);
}
