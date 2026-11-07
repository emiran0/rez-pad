using RezPad.Host.Device;
using RezPad.Protocol;

namespace RezPad.Host.Weather;

/// <summary>Sends WEATHER to the device after every fetch and on (re)connect.</summary>
internal sealed class WeatherBridge
{
    private readonly SerialLink _link;
    private readonly WeatherService _weather;

    public WeatherBridge(SerialLink link, WeatherService weather)
    {
        _link = link;
        _weather = weather;
        _weather.Updated += Send;
        _link.Connected += _ =>
        {
            if (_weather.Latest is { } latest)
            {
                Send(latest);
            }
        };
    }

    private void Send(WeatherMsg msg)
    {
        if (_link.IsConnected)
        {
            _link.SendJson(MsgType.Weather, msg);
        }
    }
}
