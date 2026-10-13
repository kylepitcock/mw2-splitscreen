using System;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace MW2Splitscreen
{
    [Flags]
    enum Btn : ushort
    {
        Up = 0x0001, Down = 0x0002, Left = 0x0004, Right = 0x0008,
        Start = 0x0010, Back = 0x0020,
        A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000,
    }

    /// <summary>Polls the four XInput slots and turns them into menu events.</summary>
    sealed class Gamepads
    {
        public const int Count = 4;

        [StructLayout(LayoutKind.Sequential)]
        struct XInputState
        {
            public uint PacketNumber;
            public ushort Buttons;
            public byte LeftTrigger, RightTrigger;
            public short ThumbLX, ThumbLY, ThumbRX, ThumbRY;
        }

        static class X14 { [DllImport("xinput1_4.dll")] public static extern int XInputGetState(int i, out XInputState s); }
        static class X910 { [DllImport("xinput9_1_0.dll")] public static extern int XInputGetState(int i, out XInputState s); }

        static bool useLegacy;

        static int GetState(int i, out XInputState s)
        {
            if (!useLegacy)
            {
                try { return X14.XInputGetState(i, out s); }
                catch (DllNotFoundException) { useLegacy = true; }
            }
            return X910.XInputGetState(i, out s);
        }

        const int StickThreshold = 16000;
        const long RepeatDelayMs = 380, RepeatRateMs = 110;
        const long DisconnectedProbeMs = 1000; // polling an empty slot is slow; do it rarely

        readonly bool[] connected = new bool[Count];
        readonly long[] nextProbe = new long[Count];
        readonly ushort[] buttons = new ushort[Count];
        readonly ushort[] previous = new ushort[Count];
        readonly int[] navX = new int[Count], navY = new int[Count];
        readonly int[] heldX = new int[Count], heldY = new int[Count];
        readonly long[] nextRepeatX = new long[Count], nextRepeatY = new long[Count];
        readonly Stopwatch clock = Stopwatch.StartNew();

        public void Poll()
        {
            long now = clock.ElapsedMilliseconds;
            for (int i = 0; i < Count; i++)
            {
                previous[i] = buttons[i];
                navX[i] = navY[i] = 0;

                if (!connected[i] && now < nextProbe[i]) continue;
                XInputState s;
                connected[i] = GetState(i, out s) == 0;
                if (!connected[i])
                {
                    buttons[i] = 0;
                    heldX[i] = heldY[i] = 0;
                    nextProbe[i] = now + DisconnectedProbeMs;
                    continue;
                }

                buttons[i] = s.Buttons;
                int dx = (Has(s.Buttons, Btn.Right) || s.ThumbLX > StickThreshold ? 1 : 0)
                       - (Has(s.Buttons, Btn.Left) || s.ThumbLX < -StickThreshold ? 1 : 0);
                int dy = (Has(s.Buttons, Btn.Down) || s.ThumbLY < -StickThreshold ? 1 : 0)
                       - (Has(s.Buttons, Btn.Up) || s.ThumbLY > StickThreshold ? 1 : 0);
                navX[i] = Repeat(dx, ref heldX[i], ref nextRepeatX[i], now);
                navY[i] = Repeat(dy, ref heldY[i], ref nextRepeatY[i], now);
            }
        }

        static int Repeat(int dir, ref int held, ref long next, long now)
        {
            if (dir == 0) { held = 0; return 0; }
            if (dir != held) { held = dir; next = now + RepeatDelayMs; return dir; }
            if (now >= next) { next = now + RepeatRateMs; return dir; }
            return 0;
        }

        static bool Has(ushort b, Btn f) => (b & (ushort)f) != 0;

        public bool Connected(int i) => connected[i];
        public bool Held(int i, Btn b) => Has(buttons[i], b);
        public bool Pressed(int i, Btn b) => Has(buttons[i], b) && !Has(previous[i], b);
        public int NavX(int i) => navX[i];
        public int NavY(int i) => navY[i];
    }
}
