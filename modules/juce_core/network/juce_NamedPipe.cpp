/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

#if ! JUCE_WASM

NamedPipe::NamedPipe() {}

NamedPipe::~NamedPipe()
{
    close();
}

bool NamedPipe::openExisting (const String& pipeName)
{
    close();

    ScopedWriteLock sl (lock);
    currentPipeName = pipeName;
    return openInternal (pipeName, false, false);
}

bool NamedPipe::isOpen() const
{
    ScopedReadLock sl (lock);
    return pimpl != nullptr;
}

bool NamedPipe::createNewPipe (const String& pipeName, bool mustNotExist)
{
    close();

    ScopedWriteLock sl (lock);
    currentPipeName = pipeName;
    return openInternal (pipeName, true, mustNotExist);
}

String NamedPipe::getName() const
{
    ScopedReadLock sl (lock);
    return currentPipeName;
}

// other methods for this class are implemented in the platform-specific files


//==============================================================================
//==============================================================================
#if JUCE_UNIT_TESTS

class NamedPipeTests final : public UnitTest
{
public:
    //==============================================================================
    NamedPipeTests()
        : UnitTest ("NamedPipe", UnitTestCategories::networking)
    {}

    void runTest() override
    {
        const auto pipeName = "TestPipe" + String ((intptr_t) Thread::getCurrentThreadId());

        beginTest ("Pre test cleanup");
        {
            NamedPipe pipe;
            expect (pipe.createNewPipe (pipeName, false));
        }

        beginTest ("Create pipe");
        {
            NamedPipe pipe;
            expect (! pipe.isOpen());

            expect (pipe.createNewPipe (pipeName, true));
            expect (pipe.isOpen());

            expect (pipe.createNewPipe (pipeName, false));
            expect (pipe.isOpen());

            NamedPipe otherPipe;
            expect (! otherPipe.createNewPipe (pipeName, true));
            expect (! otherPipe.isOpen());
        }

        beginTest ("Existing pipe");
        {
            NamedPipe pipe;

            expect (! pipe.openExisting (pipeName));
            expect (! pipe.isOpen());

            expect (pipe.createNewPipe (pipeName, true));

            NamedPipe otherPipe;
            expect (otherPipe.openExisting (pipeName));
            expect (otherPipe.isOpen());
        }

        int sendData = 4684682;

        beginTest ("Receive message created pipe");
        {
            NamedPipe pipe;
            expect (pipe.createNewPipe (pipeName, true));

            WaitableEvent senderFinished;
            SenderThread sender (pipeName, false, senderFinished, sendData);

            sender.startThread();

            int recvData = -1;
            auto bytesRead = pipe.read (&recvData, sizeof (recvData), 2000);

            expect (senderFinished.wait (4000));

            expectEquals (bytesRead, (int) sizeof (recvData));
            expectEquals (sender.result, (int) sizeof (sendData));
            expectEquals (recvData, sendData);
        }

        beginTest ("Receive message existing pipe");
        {
            WaitableEvent senderFinished;
            SenderThread sender (pipeName, true, senderFinished, sendData);

            NamedPipe pipe;
            expect (pipe.openExisting (pipeName));

            sender.startThread();

            int recvData = -1;
            auto bytesRead = pipe.read (&recvData, sizeof (recvData), 2000);

            expect (senderFinished.wait (4000));

            expectEquals (bytesRead, (int) sizeof (recvData));
            expectEquals (sender.result, (int) sizeof (sendData));
            expectEquals (recvData, sendData);
        }

        beginTest ("Send message created pipe");
        {
            NamedPipe pipe;
            expect (pipe.createNewPipe (pipeName, true));

            WaitableEvent receiverFinished;
            ReceiverThread receiver (pipeName, false, receiverFinished);

            receiver.startThread();

            auto bytesWritten = pipe.write (&sendData, sizeof (sendData), 2000);

            expect (receiverFinished.wait (4000));

            expectEquals (bytesWritten, (int) sizeof (sendData));
            expectEquals (receiver.result, (int) sizeof (receiver.recvData));
            expectEquals (receiver.recvData, sendData);
        }

        beginTest ("Send message existing pipe");
        {
            WaitableEvent receiverFinished;
            ReceiverThread receiver (pipeName, true, receiverFinished);

            NamedPipe pipe;
            expect (pipe.openExisting (pipeName));

            receiver.startThread();

            auto bytesWritten = pipe.write (&sendData, sizeof (sendData), 2000);

            expect (receiverFinished.wait (4000));

            expectEquals (bytesWritten, (int) sizeof (sendData));
            expectEquals (receiver.result, (int) sizeof (receiver.recvData));
            expectEquals (receiver.recvData, sendData);
        }
    }

private:
    //==============================================================================
    struct NamedPipeThread : public Thread
    {
        NamedPipeThread (const String& tName, const String& pName,
                         bool shouldCreatePipe, WaitableEvent& completed)
            : Thread (tName), pipeName (pName), workCompleted (completed)
        {
            if (shouldCreatePipe)
                pipe.createNewPipe (pipeName);
            else
                pipe.openExisting (pipeName);
        }

        NamedPipe pipe;
        const String& pipeName;
        WaitableEvent& workCompleted;

        int result = -2;
    };

    //==============================================================================
    struct SenderThread final : public NamedPipeThread
    {
        SenderThread (const String& pName, bool shouldCreatePipe,
                      WaitableEvent& completed, int sData)
            : NamedPipeThread ("NamePipeSender", pName, shouldCreatePipe, completed),
              sendData (sData)
        {}

        ~SenderThread() override
        {
            stopThread (100);
        }

        void run() override
        {
            result = pipe.write (&sendData, sizeof (sendData), 2000);
            workCompleted.signal();
        }

        const int sendData;
    };

    //==============================================================================
    struct ReceiverThread final : public NamedPipeThread
    {
        ReceiverThread (const String& pName, bool shouldCreatePipe,
                        WaitableEvent& completed)
            : NamedPipeThread ("NamePipeReceiver", pName, shouldCreatePipe, completed)
        {}

        ~ReceiverThread() override
        {
            stopThread (100);
        }

        void run() override
        {
            result = pipe.read (&recvData, sizeof (recvData), 2000);
            workCompleted.signal();
        }

        int recvData = -2;
    };
};

static NamedPipeTests namedPipeTests;

#endif
#endif

} // namespace juce
