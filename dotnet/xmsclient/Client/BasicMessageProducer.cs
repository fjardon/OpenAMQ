using System;
using System.Threading;
using log4net;
using IBM.XMS;
using OpenAMQ.Framing;
using OpenAMQ.XMS.Client.Message;
using OpenAMQ.XMS.Client.Protocol;
using OpenAMQ.XMS.Client.State.Listener;

namespace OpenAMQ.XMS.Client
{
    public class BasicMessageProducer : Closeable, IMessageProducer    
    {
        protected readonly ILog _logger = LogManager.GetLogger(typeof(BasicMessageProducer));

        private const int BASIC_CONTENT_TYPE = 6;

        /// <summary>
        /// If true, messages will not get a timestamp.
        /// </summary>
        private bool _disableTimestamps;

        /// <summary>
        /// Priority of messages created by this producer.
        /// </summary>
        private int _messagePriority;

        /// <summary>
        /// Time to live of messages. Specified in milliseconds but AMQ has 1 second resolution.
        ///
        private long _timeToLive;

        /// <summary>
        /// Delivery mode used for this producer.
        /// </summary>
        private DeliveryMode _deliveryMode = DeliveryMode.Persistent;

        /// <summary>
        /// The Destination used for this consumer, if specified upon creation.
        /// </summary>
        protected AMQDestination _destination;

        /// <summary>
        /// Default encoding used for messages produced by this producer.
        /// </summary>
        private string _encoding;

        /// <summary>
        /// Default encoding used for message produced by this producer.
        /// </summary>
        private string _mimeType;

        private AMQProtocolHandler _protocolHandler;

        /// <summary>
        /// True if this producer was created from a transacted session
        /// </summary>
        private bool _transacted;

        private ushort _channelId;

        /// <summary>
        /// This is an id generated by the session and is used to tie individual producers to the session. This means we
        /// can deregister a producer with the session when the producer is closed. We need to be able to tie producers
        /// to the session so that when an error is propagated to the session it can close the producer (meaning that
        /// a client that happens to hold onto a producer reference will get an error if he tries to use it subsequently).
        /// </summary>
        private long _producerId;

        /// <summary>
        /// The session used to create this producer
        /// </summary>
        private AMQSession _session;

        /// <summary>
        /// Default value for immediate flag is false, i.e. a consumer does not need to be attached to a queue
        /// </summary>
        protected const bool DEFAULT_IMMEDIATE = false;

        /// <summary>
        /// Default value for mandatory flag is true, i.e. server will not silently drop messages where no queue is
        /// connected to the exchange for the message
        /// </summary>
        protected const bool DEFAULT_MANDATORY = true;

#region Constructors

        public BasicMessageProducer(AMQDestination destination, bool transacted, ushort channelId,
                                       AMQSession session, AMQProtocolHandler protocolHandler, long producerId)            
        {
            _destination = destination;
            _transacted = transacted;
            _protocolHandler = protocolHandler;
            _channelId = channelId;
            _session = session;
            _producerId = producerId;
            if (destination != null)
            {
                DeclareDestination(destination);
            }

        }

#endregion

        private void DeclareDestination(AMQDestination destination)
        {
            // Declare the exchange
            // Note that the durable and internal arguments are ignored since passive is set to false
            AMQFrame exchangeDeclareFrame = ExchangeDeclareBody.CreateAMQFrame(_channelId, 0, destination.ExchangeName,
                                                                               destination.ExchangeClass, false,
                                                                               false, false, false, null);
            _protocolHandler.WriteCommandFrameAndWaitForReply(exchangeDeclareFrame,
                                                              new SpecificMethodFrameListener(_channelId,
                                                                                              typeof(ExchangeDeclareOkBody)));
        }

        #region IMessageProducer Members

        public DeliveryMode DeliveryMode
        {
            get
            {
                CheckNotClosed();
                return _deliveryMode;
            }
            set
            {
                CheckNotClosed();                
                _deliveryMode = value;
            }
        }

        public IDestination Destination
        {
            get
            {
                return _destination;
            }
        }

        public bool DisableMessageID
        {
            get
            {
                throw new Exception("The method or operation is not implemented.");
            }
            set
            {
                throw new Exception("The method or operation is not implemented.");
            }
        }

        public bool DisableMessageTimestamp
        {
            get
            {
                CheckNotClosed();
                return _disableTimestamps;
            }
            set
            {
                CheckNotClosed();
                _disableTimestamps = value;
            }
        }

        public int Priority
        {
            get
            {
                CheckNotClosed();
                return _messagePriority;
            }
            set
            {
                CheckNotClosed();
                if (value < 0 || value > 9)
                {
                    throw new ArgumentOutOfRangeException("Priority of " + value + " is illegal. Value must be in range 0 to 9");
                }
                _messagePriority = value;
            }
        }
           
        public override void Close()
        {
            Interlocked.Exchange(ref _closed, CLOSED);
            _session.DeregisterProducer(_producerId);
        }
    
        public void Send(IDestination dest, IMessage msg, DeliveryMode deliveryMode, int priority, long timeToLive)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public void Send(IDestination dest, IMessage msg)
        {
            CheckNotClosed();
            ValidateDestination(dest);
            SendImpl((AMQDestination) dest, (AbstractXMSMessage) msg, _deliveryMode, _messagePriority, (uint)_timeToLive,
                     DEFAULT_MANDATORY, DEFAULT_IMMEDIATE);
        }

        public void Send(IMessage msg, DeliveryMode deliveryMode, int priority, long timeToLive)
        {
            SendImpl(_destination, (AbstractXMSMessage) msg, deliveryMode, priority, (uint)timeToLive, DEFAULT_MANDATORY,
                     DEFAULT_IMMEDIATE);
        }

        public void Send(IMessage msg)
        {
            SendImpl(_destination, (AbstractXMSMessage) msg, _deliveryMode, _messagePriority, (uint)_timeToLive,
                     DEFAULT_MANDATORY, DEFAULT_IMMEDIATE);
        }

        public void Send(IDestination destination, IMessage message, DeliveryMode deliveryMode, int priority,
                         long timeToLive, bool immediate)
        {
            SendImpl((AMQDestination) destination, (AbstractXMSMessage) message, deliveryMode, priority, (uint) timeToLive, DEFAULT_MANDATORY,
                     immediate);
        }

        public void Send(IDestination destination, IMessage message, DeliveryMode deliveryMode, int priority,
                         long timeToLive, bool mandatory, bool immediate)
        {
            throw new NotImplementedException();
        }
        
        public long TimeToLive
        {
            get
            {
                CheckNotClosed();
                return _timeToLive;
            }
            set
            {
                CheckNotClosed();
                if (value < 0)
                {
                    throw new ArgumentOutOfRangeException("Time to live must be non-negative - supplied value was " + value);
                }
                _timeToLive = value;
            }
        }

        #endregion

        private void ValidateDestination(IDestination destination)
        {
            if (!(destination is AMQDestination))
            {
                throw new XMSException("Unsupported destination class: " +
                                       (destination != null?destination.GetType():null));
            }
            try
            {
                DeclareDestination((AMQDestination)destination);
            }
            catch (AMQException e)
            {
                throw new XMSException("Unable to declare destination " + destination + ": " + e);
            }
        }

        protected void SendImpl(AMQDestination destination, AbstractXMSMessage message, DeliveryMode deliveryMode, int priority,
                                uint timeToLive, bool mandatory, bool immediate)
        {
            AMQFrame publishFrame = BasicPublishBody.CreateAMQFrame(_channelId, 0, destination.ExchangeName,
                                                                    destination.Name, mandatory, immediate);

            long currentTime = 0;
            if (!_disableTimestamps)
            {
                currentTime = DateTime.UtcNow.Ticks;
                message.JMSTimestamp = currentTime;
            }
            byte[] payload = message.Data;
            BasicContentHeaderProperties contentHeaderProperties = message.XmsContentHeaderProperties;

            if (timeToLive > 0)
            {
                if (!_disableTimestamps)
                {
                    contentHeaderProperties.Expiration = (uint) currentTime + timeToLive;
                }
            }
            else
            {
                contentHeaderProperties.Expiration = 0;
            }
            contentHeaderProperties.SetDeliveryMode(deliveryMode);
            contentHeaderProperties.Priority = (byte) priority;

            ContentBody[] contentBodies = CreateContentBodies(payload);
            AMQFrame[] frames = new AMQFrame[2 + contentBodies.Length];
            for (int i = 0; i < contentBodies.Length; i++)
            {
                frames[2 + i] = ContentBody.CreateAMQFrame(_channelId, contentBodies[i]);
            }
            if (contentBodies.Length > 0 && _logger.IsDebugEnabled)
            {
                _logger.Debug("Sending content body frames to " + destination);
            }

            // weight argument of zero indicates no child content headers, just bodies
            AMQFrame contentHeaderFrame = ContentHeaderBody.CreateAMQFrame(_channelId, BASIC_CONTENT_TYPE, 0, contentHeaderProperties,
                                                                           (uint)payload.Length);
            if (_logger.IsDebugEnabled)
            {
                _logger.Debug("Sending content header frame to " + destination);
            }

            frames[0] = publishFrame;
            frames[1] = contentHeaderFrame;
            CompositeAMQDataBlock compositeFrame = new CompositeAMQDataBlock(frames);
            _protocolHandler.WriteFrame(compositeFrame);
        }

        /// <summary>
        /// Create content bodies. This will split a large message into numerous bodies depending on the negotiated
        /// maximum frame size.
        /// </summary>
        /// <param name="payload"></param>
        /// <returns>return the array of content bodies</returns>
        private ContentBody[] CreateContentBodies(byte[] payload)
        {
            if (payload == null)
            {
                return null;
            }
            else if (payload.Length == 0)
            {
                return new ContentBody[0];
            }
            // we substract one from the total frame maximum size to account for the end of frame marker in a body frame
            // (0xCE byte).
            long framePayloadMax = _session.Connection.MaximumFrameSize - 1;
            int lastFrame = (payload.Length % framePayloadMax) > 0 ? 1 : 0;
            int frameCount = (int) (payload.Length/framePayloadMax) + lastFrame;
            ContentBody[] bodies = new ContentBody[frameCount];

            if (frameCount == 1)
            {
                bodies[0] = new ContentBody();
                bodies[0].Payload = payload;
            }
            else
            {
                long remaining = payload.Length;
                for (int i = 0; i < bodies.Length; i++)
                {
                    bodies[i] = new ContentBody();
                    byte[] framePayload = new byte[(remaining >= framePayloadMax) ? (int)framePayloadMax : (int)remaining];
                    Array.Copy(payload, (int)framePayloadMax * i, framePayload, 0, framePayload.Length);
                    bodies[i].Payload = framePayload;
                    remaining -= framePayload.Length;
                }
            }
            return bodies;
        }

        public string MimeType
        {
            set
            {
                CheckNotClosed();
                _mimeType = value;
            }
        }

        public string Encoding
        {
            set
            {
                CheckNotClosed();
                _encoding = value;
            }
        }

        #region IPropertyContext Members

        public bool GetBooleanProperty(string property_name)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public byte GetByteProperty(string property_name)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public byte[] GetBytesProperty(string property_name)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public char GetCharProperty(string property_name)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public double GetDoubleProperty(string property_name)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public float GetFloatProperty(string property_name)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public int GetIntProperty(string property_name)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public long GetLongProperty(string property_name)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public object GetObjectProperty(string property_name)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public short GetShortProperty(string property_name)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public short GetSignedByteProperty(string property_name)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public string GetStringProperty(string property_name)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public bool PropertyExists(string propertyName)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public void SetBooleanProperty(string property_name, bool value)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public void SetByteProperty(string property_name, byte value)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public void SetBytesProperty(string property_name, byte[] value)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public void SetCharProperty(string property_name, char value)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public void SetDoubleProperty(string property_name, double value)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public void SetFloatProperty(string property_name, float value)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public void SetIntProperty(string property_name, int value)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public void SetLongProperty(string property_name, long value)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public void SetObjectProperty(string property_name, object value)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public void SetShortProperty(string property_name, short value)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public void SetSignedByteProperty(string property_name, short value)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        public void SetStringProperty(string property_name, string value)
        {
            throw new Exception("The method or operation is not implemented.");
        }

        #endregion

        #region IDisposable Members

        public void Dispose()
        {
            throw new Exception("The method or operation is not implemented.");
        }

        #endregion
        
    }
}
