using System.Text.Json.Nodes;
using RezPad.Protocol;
using Xunit;

namespace RezPad.Protocol.Tests;

/// <summary>Records shaped like a Beszel 0.21.1 hub with 0.17 and 0.21 agents (values made up).</summary>
public class BeszelMapTests
{
    private static readonly JsonArray Items = (JsonArray)JsonNode.Parse("""
        [
          {"id": "sys1", "name": "Main PVE Node", "status": "up",
           "info": {"cpu": 1.07, "mp": 27.91, "dp": 15.65, "dt": 40.85, "u": 962181, "bb": 2425708, "la": [0.14, 0.15, 0.18]}},
          {"id": "sys2", "name": "Old NAS", "status": "up",
           "info": {"cpu": 4.36, "mp": 41.7, "dp": 0.4, "b": 0.66, "u": 3600, "la": [0.38, 0.35, 0.31], "efs": {"pool-main": 27.41, "Media": 21.04}}},
          {"id": "sys3", "name": "Relay", "status": "down", "info": {"cpu": 2.0}}
        ]
        """)!;

    private static readonly Dictionary<string, JsonObject> Minute = new()
    {
        ["sys1"] = (JsonObject)JsonNode.Parse("""
            {"stats": {"mu": 8.53, "du": 13.94, "b": [335635, 765745],
                       "t": {"acpitz": 27.8, "coretemp_core_0": 36, "coretemp_package_id_0": 38, "nvme_composite": 40.85}}}
            """)!,
        ["sys2"] = (JsonObject)JsonNode.Parse("""{"stats": {"t": {"cpu_thermal": 51.25, "nvme_composite": 39.85}}}""")!,
    };

    [Fact]
    public void MapsInfoAndMinuteStats()
    {
        var r = BeszelMap.Compute(Items, Minute);
        Assert.Equal(["main-pve-node", "old-nas", "relay"], r.Systems.Select(s => s.Slug));
        var v = r.Values;
        Assert.Equal(1, v["bz.main-pve-node.online"]);
        Assert.Equal(1.07, v["bz.main-pve-node.cpu_load"]);
        Assert.Equal(27.91, v["bz.main-pve-node.mem_used_pct"]);
        Assert.Equal(8.53, v["bz.main-pve-node.mem_used_gb"]);
        Assert.Equal(0.18, v["bz.main-pve-node.load15"]);
        Assert.Equal(2425.7, v["bz.main-pve-node.net_kbps"]);   // bb is bytes/s
        Assert.Equal(335.6, v["bz.main-pve-node.net_tx_kbps"]);
        Assert.Equal(765.7, v["bz.main-pve-node.net_rx_kbps"]);
        Assert.Equal(40.85, v["bz.main-pve-node.temp"]);
        Assert.Equal(38, v["bz.main-pve-node.cpu_temp"]);       // the package sensor, not the hottest
        Assert.Equal(267.3, v["bz.main-pve-node.uptime_h"]);
        Assert.Equal(660, v["bz.old-nas.net_kbps"]);              // old agents: b in MB/s
        Assert.Equal(51.25, v["bz.old-nas.cpu_temp"]);
        Assert.Null(v["bz.old-nas.net_tx_kbps"]);                 // no minute record field
    }

    [Fact]
    public void ExtraFilesystemsAreSortedByName()
    {
        var r = BeszelMap.Compute(Items, Minute);
        Assert.Equal(21.04, r.Values["bz.old-nas.fs1_used_pct"]);
        Assert.Equal(27.41, r.Values["bz.old-nas.fs2_used_pct"]);
        Assert.Equal("Old NAS disk Media", r.Sources.Single(s => s.Id == "bz.old-nas.fs1_used_pct").Name);
        Assert.DoesNotContain(r.Sources, s => s.Id == "bz.main-pve-node.fs1_used_pct");
    }

    [Fact]
    public void DownSystemsKeepTheirSourcesWithoutValues()
    {
        var r = BeszelMap.Compute(Items, Minute);
        Assert.Equal(0, r.Values["bz.relay.online"]);
        Assert.Null(r.Values["bz.relay.cpu_load"]);
        Assert.Contains(r.Sources, s => s.Id == "bz.relay.cpu_load" && s.Group == "Beszel: Relay");
    }

    [Fact]
    public void EveryIdFitsTheDevice()
    {
        var items = (JsonArray)JsonNode.Parse("""[{"id": "x", "name": "an-extremely-long-machine-name", "status": "up", "info": {"efs": {"a": 1, "b": 2, "c": 3}}}]""")!;
        Assert.All(BeszelMap.Compute(items, new Dictionary<string, JsonObject>()).Sources, s => Assert.True(s.Id.Length <= 31, s.Id));
    }

    [Theory]
    [InlineData("""{"k10temp_tctl": 61, "nvme": 40}""", 61.0)]
    [InlineData("""{"coretemp_core_0": 44, "coretemp_core_1": 52}""", 52.0)]
    [InlineData("""{"nvme_composite": 40}""", null)]
    public void CpuTemperatureSensor(string sensors, double? expected) =>
        Assert.Equal(expected, BeszelMap.CpuTemp((JsonObject)JsonNode.Parse(sensors)!));
}
