using System;
using jpmorgan.mina.common;
using log4net;

namespace OpenAMQ.XMS.Client
{
    public abstract class Closeable
    {
        /// <summary>
        /// Used to ensure orderly closing of the object. The only method that is allowed to be called
        /// from another thread of control is close().
        /// </summary>
        protected readonly object _closingLock = new object();

        /// <summary>
        /// All access to this field should be using the Inerlocked class, to make it atomic.
        /// Hence it is an int since you cannot use a bool with the Interlocked class.
        /// </summary>
        protected volatile int _closed = NOT_CLOSED;

        protected const int CLOSED = 1;
        protected const int NOT_CLOSED = 2;

        /// <summary>
        /// Checks the not closed.
        /// </summary>
        protected void CheckNotClosed()
        {
            if (_closed == CLOSED)
            {
                throw new InvalidOperationException("Object " + ToString() + " has been closed");
            }
        }

        /// <summary>
        /// Gets a value indicating whether this <see cref="T:Closeable"/> is closed.
        /// </summary>
        /// <value><c>true</c> if closed; otherwise, <c>false</c>.</value>
        public bool Closed
        {
            get
            {
                return _closed;
            }
        }

        /// <summary>
        /// Close the resource
        /// </summary>
        /// <exception cref="XMSException">If something goes wrong</exception>
        public abstract void Close();
    }
}