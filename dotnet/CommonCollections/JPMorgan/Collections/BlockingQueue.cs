using System;
using System.Collections;
using System.Text;

namespace JPMorgan.Collections
{
    public abstract class BlockingQueue : Queue
    {        
        /**
         * Inserts the specified element into this queue if it is possible to do
         * so immediately without violating capacity restrictions, returning
         * <tt>true</tt> upon success and <tt>false</tt> if no space is currently
         * available.  When using a capacity-restricted queue, this method is
         * generally preferable to {@link #add}, which can fail to insert an
         * element only by throwing an exception.
         *
         * @param e the element to add
         * @return <tt>true</tt> if the element was added to this queue, else
         *         <tt>false</tt>
         * @throws ClassCastException if the class of the specified element
         *         prevents it from being added to this queue
         * @throws NullPointerException if the specified element is null
         * @throws IllegalArgumentException if some property of the specified
         *         element prevents it from being added to this queue
         */
        public abstract bool EnqueueNoThrow(Object e);

        /**
         * Inserts the specified element into this queue, waiting if necessary
         * for space to become available.
         *
         * @param e the element to add
         * @throws InterruptedException if interrupted while waiting
         * @throws ClassCastException if the class of the specified element
         *         prevents it from being added to this queue
         * @throws NullPointerException if the specified element is null
         * @throws IllegalArgumentException if some property of the specified
         *         element prevents it from being added to this queue
         */
        public abstract void EnqueueBlocking(object e);                     

        /**
         * Retrieves and removes the head of this queue, waiting up to the
         * specified wait time if necessary for an element to become available.
         *
         * @param timeout how long to wait before giving up, in units of
         *        <tt>unit</tt>
         * @param unit a <tt>TimeUnit</tt> determining how to interpret the
         *        <tt>timeout</tt> parameter
         * @return the head of this queue, or <tt>null</tt> if the
         *         specified waiting time elapses before an element is available
         * @throws InterruptedException if interrupted while waiting
         */
        public abstract object DequeueBlocking();            

        /**
         * Returns the number of additional elements that this queue can ideally
         * (in the absence of memory or resource constraints) accept without
         * blocking, or <tt>Integer.MAX_VALUE</tt> if there is no intrinsic
         * limit.
         *
         * <p>Note that you <em>cannot</em> always tell if an attempt to insert
         * an element will succeed by inspecting <tt>remainingCapacity</tt>
         * because it may be the case that another thread is about to
         * insert or remove an element.
         *
         * @return the remaining capacity
         */
        public abstract int RemainingCapacity
        {
            get;
        }                        
    }
}
