package org.openamq.client.message;

import org.openamq.client.AMQException;
import org.openamq.client.framing.AMQMessage;

import javax.jms.JMSException;
import java.util.HashMap;
import java.util.Map;

/**
 * @author Robert Greig (robert.j.greig@jpmorgan.com)
 */
public class MessageFactoryRegistry
{
    private final Map _mimeToFactoryMap = new HashMap();

    public void registerFactory(String mimeType, MessageFactory mf)
    {
        if (mimeType == null)
        {
            throw new IllegalArgumentException("Mime time must not be null");
        }
        if (mf == null)
        {
            throw new IllegalArgumentException("Message factory must not be null");
        }
        _mimeToFactoryMap.put(mimeType, mf);
    }

    public MessageFactory deregisterFactory(String mimeType)
    {
        return (MessageFactory) _mimeToFactoryMap.remove(mimeType);
    }

    public AbstractMessage createMessage(long messageNbr, boolean redelivered,
                                         AMQMessage frame) throws AMQException, JMSException
    {
        MessageFactory mf = (MessageFactory) _mimeToFactoryMap.get(frame.mimeType);
        if (mf == null)
        {
            throw new AMQException("Unsupport MIME type of " + frame.mimeType);
        }
        else
        {
            return mf.createMessage(messageNbr, redelivered, frame);
        }
    }

    public AbstractMessage createMessage(String mimeType) throws AMQException, JMSException
    {
        if (mimeType == null)
        {
            throw new IllegalArgumentException("Mime type must not be null");
        }
        MessageFactory mf = (MessageFactory) _mimeToFactoryMap.get(mimeType);
        if (mf == null)
        {
            throw new AMQException("Unsupport MIME type of " + mimeType);
        }
        else
        {
            return mf.createMessage();
        }
    }

    /**
     * Construct a new registry with the default message factories registered
     * @return a message factory registry
     */
    public static MessageFactoryRegistry newDefaultRegistry()
    {
        MessageFactoryRegistry mf = new MessageFactoryRegistry();
        mf.registerFactory("text/plain", new AMQTextMessageFactory());
        return mf;
    }
}