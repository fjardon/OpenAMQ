using System;
using jpmorgan.mina.common;
using log4net;
using OpenAMQ.Framing;
using OpenAMQ.XMS.Client.Protocol;

namespace OpenAMQ.XMS.Client.State.Listener
{
    public class ConnectionCloseOkListener : BlockingMethodFrameListener
    {

        public ConnectionCloseOkListener()
            : base(0)
        {
        }

        public override bool ProcessMethod(ushort channelId, AMQMethodBody frame)
        {
            return (frame is ConnectionCloseOkBody);
        }
    }
}