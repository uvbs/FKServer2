#include "stdafx.h"

#include "Player.h"
#include "Socket.h"
#include "PacketFactoryManager.h"
#include "TimeManager.h"
#include "Log.h"

using namespace Packets ;

Player::Player( )
{
__ENTER_FUNCTION

	m_PID = INVALID_ID ;
	m_UID = INVALID_ID ;
	m_PlayerManagerID = INVALID_ID ;


	m_pSocket = new Socket ;
	Assert( m_pSocket ) ;

	m_pSocketInputStream = new SocketInputStream( m_pSocket,DEFAULTSOCKETINPUTBUFFERSIZE,64*1024*1024 ) ;
	Assert( m_pSocketInputStream ) ;

	m_pSocketOutputStream = new SocketOutputStream( m_pSocket,DEFAULTSOCKETOUTPUTBUFFERSIZE,64*1024*1024 ) ;
	Assert( m_pSocketOutputStream ) ;

	m_IsEmpty		= TRUE ;
	m_IsDisconnect	= FALSE ;

	m_PacketIndex	 = 0 ;

__LEAVE_FUNCTION
}

Player::~Player( )
{
__ENTER_FUNCTION

	SAFE_DELETE( m_pSocketInputStream ) ;
	SAFE_DELETE( m_pSocketOutputStream ) ;

	SAFE_DELETE( m_pSocket ) ;

__LEAVE_FUNCTION
}

void Player::CleanUp( )
{
__ENTER_FUNCTION

	m_pSocket->close() ;
	m_pSocketInputStream->CleanUp() ;
	m_pSocketOutputStream->CleanUp() ;
	SetPlayerManagerID( INVALID_ID ) ;
	SetUserID( INVALID_ID ) ;
	m_PacketIndex = 0 ;
	SetDisconnect(FALSE) ;


__LEAVE_FUNCTION
}

void Player::Disconnect( )
{
__ENTER_FUNCTION

	_MY_TRY
	{
		m_pSocket->close() ;
	}
	_MY_CATCH
	{
	}

__LEAVE_FUNCTION
}

BOOL Player::IsValid( )
{
__ENTER_FUNCTION

	if( m_pSocket==NULL ) return FALSE ;

	if( !m_pSocket->isValid() ) return FALSE ;


	return TRUE ;

__LEAVE_FUNCTION

	return FALSE ;
}

BOOL Player::ProcessInput( )
{
__ENTER_FUNCTION

	if( IsDisconnect() )
		return TRUE ;

	_MY_TRY 
	{
		UINT ret = m_pSocketInputStream->Fill( ) ;
		if( (int)ret <= SOCKET_ERROR )
		{
			Log::SaveLog( SERVER_ERRORFILE, "[%d] m_pSocketInputStream->Fill ret:%d %s", 
				g_pTimeManager->Time2DWORD(), (int)ret, MySocketError() ) ;
			return FALSE ;
		}
	} 
	_MY_CATCH
	{
		return FALSE ;
	}


	return TRUE ;

__LEAVE_FUNCTION

	return FALSE ;
}

BOOL Player::ProcessCommand( bool Option )
{
__ENTER_FUNCTION

	BOOL ret ;

	char header[PACKET_HEADER_SIZE];
	PacketID_t packetID;
	UINT packetuint, packetSize, packetIndex, packetTick;
	Packet* pPacket = NULL ;

	if( IsDisconnect( ) )
		return TRUE ;

	_MY_TRY
	{
		if( Option ) 
		{//ִ�в���ѡ�����
		}

//ÿ֡����ִ�е���Ϣ��������
#define EXE_COUNT_PER_TICK 120
		for( INT i=0;i<EXE_COUNT_PER_TICK; i++ )
		{
			if( !m_pSocketInputStream->Peek(&header[0], PACKET_HEADER_SIZE) )
			{//���ݲ��������Ϣͷ
				break ;
			}

			memcpy( &packetID, &header[0], sizeof(PacketID_t) ) ;	
			memcpy( &packetTick, &header[sizeof(UINT)], sizeof(UINT) ) ;
			memcpy( &packetuint, &header[sizeof(UINT) + sizeof(PacketID_t)], sizeof(UINT) );
			packetSize = GET_PACKET_LEN(packetuint) ;
			packetIndex = GET_PACKET_INDEX(packetuint) ;

			if( packetID >= (PacketID_t)PACKET_MAX )
			{//��Ч����Ϣ����
				Assert( FALSE ) ;
				return FALSE ;
			}

			_MY_TRY
			{

				if( m_pSocketInputStream->Length()<PACKET_HEADER_SIZE+packetSize )
				{//��Ϣû�н���ȫ
					break;
				}

				if( packetSize>g_pPacketFactoryManager->GetPacketMaxSize(packetID) )
				{//��Ϣ�Ĵ�С�����쳣���յ�����Ϣ��Ԥ������Ϣ�����ֵ��Ҫ��
					Assert( FALSE ) ;
					return FALSE ;
				}

				Packet* pPacket = g_pPacketFactoryManager->CreatePacket( packetID ) ;
				if( pPacket==NULL )
				{//���ܷ��䵽�㹻���ڴ�
					Assert( FALSE ) ;
					return FALSE ;
				}

				//������Ϣ���к�
				pPacket->SetPacketIndex( packetIndex ) ;

				ret = m_pSocketInputStream->ReadPacket( pPacket ) ;
				if( ret==FALSE )
				{//��ȡ��Ϣ���ݴ���
					Assert( FALSE ) ;
					g_pPacketFactoryManager->RemovePacket( pPacket ) ;
					return FALSE ;
				}
		Log::SaveLog( "./Log/World��.txt", "���հ� [��Դ�˿ڣ�%d, ID=%d��size=%d]", 
			m_pSocketInputStream->m_pSocket->m_Port, pPacket->GetPacketID() ,pPacket->GetPacketSize ()) ;

				BOOL bNeedRemove = TRUE ;

				_MY_TRY
				{
					//����m_KickTime��Ϣ��m_KickTime��Ϣ�е�ֵΪ�ж��Ƿ���Ҫ�ߵ�
					//�ͻ��˵�����
					ResetKick( ) ;

					UINT uret ;
					_MY_TRY
					{
						uret = pPacket->Execute( this ) ;
					}
					_MY_CATCH
					{
						uret=PACKET_EXE_ERROR ;
					}
					if( uret==PACKET_EXE_ERROR )
					{//�����쳣���󣬶Ͽ����������
						if( pPacket ) 
							g_pPacketFactoryManager->RemovePacket( pPacket ) ;
						return FALSE ;
					}
					else if( uret==PACKET_EXE_BREAK )
					{//��ǰ��Ϣ�Ľ���ִ�н�ֹͣ
					 //ֱ���¸�ѭ��ʱ�ż����Ի����е����ݽ�����Ϣ��ʽ
					 //����ִ�С�
					 //����Ҫ���ͻ��˵�ִ�д�һ������ת�Ƶ�����һ������ʱ��
					 //��Ҫ�ڷ���ת����Ϣ��ִ���ڱ��߳���ֹͣ��
						if( pPacket ) 
							g_pPacketFactoryManager->RemovePacket( pPacket ) ;
						break ;
					}
					else if( uret==PACKET_EXE_CONTINUE )
					{//��������ʣ�µ���Ϣ
					}
					else if( uret==PACKET_EXE_NOTREMOVE )
					{//��������ʣ�µ���Ϣ�����Ҳ����յ�ǰ��Ϣ
						bNeedRemove = FALSE ;
					}
					else if( uret==PACKET_EXE_NOTREMOVE_ERROR )
					{
						return FALSE ;
					}
					else
					{//δ֪�ķ���ֵ
						Assert(FALSE) ;
					}
				}
				_MY_CATCH
				{
				}

				if( pPacket && bNeedRemove ) 
					g_pPacketFactoryManager->RemovePacket( pPacket ) ;
			}
			_MY_CATCH
			{
			}
		}
	}
	_MY_CATCH
	{
	}



	return TRUE ;

__LEAVE_FUNCTION

	return FALSE ;
}

BOOL Player::ProcessOutput( )
{
__ENTER_FUNCTION

	if( IsDisconnect( ) )
		return TRUE ;

	_MY_TRY
	{
		UINT size = m_pSocketOutputStream->Length() ;
		if( size==0 )
		{
			return TRUE ;
		}
//		else if( size < MAX_SEND_SIZE )
//		{//�����е�����С��һ������ʱ������ÿ�ζ���������
//			if( m_CurrentTime < m_LastSendTime+MAX_SEND_TIME )
//			{//�ж���һ�η�������������ʱ���Ƿ񳬹�һ��ʱ�䣬����������򲻷�������
//				return TRUE ;
//			}
//		}
//		m_LastSendTime = m_CurrentTime ;

		UINT ret = m_pSocketOutputStream->Flush( ) ;
		if( (int)ret <= SOCKET_ERROR )
		{
			Log::SaveLog( SERVER_ERRORFILE, "[%d] m_pSocketOutputStream->Flush ret:%d %s", 
				g_pTimeManager->Time2DWORD(), (int)ret, MySocketError() ) ;
			return FALSE ;
		}
	} 
	_MY_CATCH
	{
		return FALSE ;
	}

	return TRUE ;

__LEAVE_FUNCTION

	return FALSE ;
}


BOOL Player::SendPacket( Packet* pPacket )
{
__ENTER_FUNCTION

	if( IsDisconnect( ) )
		return TRUE ;

	if( m_pSocketOutputStream!=NULL )
	{
		pPacket->SetPacketIndex( m_PacketIndex++ ) ;

		UINT nSizeBefore = m_pSocketOutputStream->Length();
		//BOOL ret = m_pSocketOutputStream->WritePacket( pPacket ) ;

		PacketID_t packetID = pPacket->GetPacketID() ;
		UINT w = m_pSocketOutputStream->Write( (CHAR*)&packetID , sizeof(PacketID_t) ) ;

		UINT packetTick = g_pTimeManager->RunTick();
		w = m_pSocketOutputStream->Write( (CHAR*)&packetTick , sizeof(UINT) ) ;

		UINT packetUINT ;
		UINT packetSize = pPacket->GetPacketSize( ) ;
		UINT packetIndex = pPacket->GetPacketIndex( ) ;

		SET_PACKET_INDEX(packetUINT, packetIndex) ;
		SET_PACKET_LEN(packetUINT, packetSize) ;

		w = m_pSocketOutputStream->Write( (CHAR*)&packetUINT, sizeof(UINT) ) ;

		BOOL ret = pPacket->Write( *m_pSocketOutputStream ) ;
		Assert( ret ) ;

		Log::SaveLog( "./Log/World��.txt", "���Ͱ� [Ŀ��˿ڣ�%d, ID=%d��size=%d]", 
			m_pSocketOutputStream->m_pSocket->m_Port, pPacket->GetPacketID() ,pPacket->GetPacketSize ()) ;

		UINT nSizeAfter = m_pSocketOutputStream->Length();

		if(pPacket->GetPacketSize() != nSizeAfter-nSizeBefore-PACKET_HEADER_SIZE)
		{
			Log::SaveLog( WORLD_LOGFILE, "!!!!!!!PacketSizeError! ID=%d(Write%d,Should%d)", 
				pPacket->GetPacketID(), 
				nSizeAfter-nSizeBefore-PACKET_HEADER_SIZE, pPacket->GetPacketSize());
		}
		Log::SaveLog( WORLD_LOGFILE, "SendPacket! ID=%d", pPacket->GetPacketID() ) ;

	}

	return TRUE ;

__LEAVE_FUNCTION

	return FALSE ;
}

BOOL Player::HeartBeat( DWORD dwTime )
{
__ENTER_FUNCTION



	return TRUE ;

__LEAVE_FUNCTION

	return FALSE ;
}

void Player::ResetKick( )
{
__ENTER_FUNCTION
__LEAVE_FUNCTION
}

