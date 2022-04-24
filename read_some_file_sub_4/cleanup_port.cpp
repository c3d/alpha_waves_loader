#include "emu.hpp"
#include "types.hpp"
#include "helper.hpp"

namespace cleanup
{
#pragma pack( push, 1 )
    struct pack_block_t
    {
        uint8_t packed_size{};
        uint8_t flag{};      // 1 = more data to uncompress, 0 = end
        uint16_t data_len{}; // (un)compressed data
        /*
		if packed_size != 0
            table0[ 1 + pack_block.packed_size ]
		    table1[ 1 + pack_block.packed_size ]
		    table2[ 1 + pack_block.packed_size ]
		data[ pack_block.data_len ]
		*/
    };
#pragma pack( pop )
    static_assert( sizeof( pack_block_t ) == 4, "wrong size" );

    struct tables_t
    {
        // from file
        std::vector<uint8_t> table0;
        std::vector<uint8_t> table1;
        std::vector<uint8_t> table2;
        std::vector<uint8_t> data;

        //prepared
        std::array<uint8_t, 256> table3{}; // needs to be 0 filled
        std::vector<uint8_t> table4;
    };

    void emu_GAME_START_sub_3( std::vector<uint8_t>& uncompressed_buffer_,
                               const std::vector<uint8_t>& compressed_buffer_ )
    {
        uint8_t* uncompressed = uncompressed_buffer_.data();
        const uint8_t* compressed = compressed_buffer_.data();

        while( true )
        {
            const pack_block_t pack_block = *reinterpret_cast<const pack_block_t*>( compressed );
            compressed += sizeof( pack_block );
            assert( pack_block.flag == 0 || pack_block.flag == 1 );

            printf( "pack_block: packed_size: 0x%02X, flag: 0x%02X, data_len: 0x%04X\n", pack_block.packed_size,
                    pack_block.flag, pack_block.data_len );

            if( pack_block.packed_size == 0 )
            {
                // uncompressed (just append the data)

                printf( " uncompressed data\n" );

                ::memcpy( uncompressed, compressed, pack_block.data_len );
                uncompressed += pack_block.data_len;
                compressed += pack_block.data_len;
                continue;
            }

            // compressed - unpack

            // some sort of offset/value maps

            tables_t tables;
            // from uncompressed data right after packed_block
            std::vector<uint8_t> table0( 1 + pack_block.packed_size );
            std::vector<uint8_t> table1( 1 + pack_block.packed_size );
            std::vector<uint8_t> table2( 1 + pack_block.packed_size );
            std::vector<uint8_t> data( pack_block.data_len );

            ::memcpy( &table0[1], compressed, pack_block.packed_size );
            compressed += pack_block.packed_size;

            ::memcpy( &table1[1], compressed, pack_block.packed_size );
            compressed += pack_block.packed_size;

            ::memcpy( &table2[1], compressed, pack_block.packed_size );
            compressed += pack_block.packed_size;

            ::memcpy( data.data(), compressed, pack_block.data_len );
            compressed += pack_block.data_len;

            // runtime filled
            std::array<uint8_t, 256> table3{}; // needs to be 0 filled
            std::vector<uint8_t> table4( 1 + pack_block.packed_size );

            for( uint16_t i = 0; i < pack_block.packed_size; ++i )
            {
                const uint8_t ofs_0 = i + 1;
                const uint8_t ofs_1 = table0[ofs_0];
                table4[ofs_0] = table3[ofs_1];
                table3[ofs_1] = ofs_0;
            }

            auto val_3_non_0 = [&table1, &table2, &table3, &table4]( uint8_t*& uncompressed, const uint8_t val_3_ ) {
                uint8_t val_4 = 0;
                uint8_t val_7 = 0;

                struct stack_vals_t
                {
                    uint8_t val_0{};
                    uint8_t val_1{};
                };
                std::stack<stack_vals_t> stack;

                auto loc_128_block = [&]() {
                    stack.push( { val_7, table2[val_7] } );
                    val_4 = table1[val_7];
                };

                auto loc_572_block = [&]() {
                    *uncompressed++ = val_4;

                    const stack_vals_t stack_val = stack.top();
                    stack.pop();

                    if( ( stack_val.val_0 == 0 ) && ( stack_val.val_1 == 0 ) )
                    {
                        return true; // goto loc_124;
                    }

                    val_7 = stack_val.val_0;
                    val_4 = stack_val.val_1;
                    return false;
                };

                val_7 = val_3_;

                stack.push( { 0, 0 } ); // end mark

                loc_128_block(); // stack push

                bool end_inner_loop = false;
                while( true )
                {
                    const uint8_t val_5 = val_4;

                    const uint8_t val_6 = table3[val_5];

                    if( val_6 == 0 )
                    {
                        if( loc_572_block() ) // stack pop
                        {
                            end_inner_loop = true;
                            break;
                        }
                    }
                    else if( val_7 > val_6 )
                    {
                        val_7 = val_6;

                        loc_128_block(); // stack push
                    }
                    else
                    {
                        val_4 = val_7;
                        val_7 = val_6;

                        while( true )
                        {
                            val_7 = table4[val_7];

                            if( val_7 == 0 )
                            {
                                val_4 = val_5;
                                if( loc_572_block() ) // stack pop
                                {
                                    end_inner_loop = true;
                                }
                                break;
                            }
                            else if( val_7 < val_4 )
                            {
                                loc_128_block(); // stack push
                                break;
                            }

                            // another run
                        }
                    }
                    if( end_inner_loop )
                    {
                        break;
                    }
                }
            };

            for( size_t i = 0; i < data.size(); ++i )
            {
                const uint8_t val_2 = data[i];
                const uint8_t val_3 = table3[val_2];

                if( val_3 == 0 )
                {
                    *uncompressed++ = val_2;
                }
                else
                {
                    val_3_non_0( uncompressed, val_3 );
                }
            }

            if( pack_block.flag == 0 )
            {
                return;
            }
        }
    }

    size_t emu_read_some_file_sub_4( emu_t& e,
                                     config_tat_t::executable_info_t* exec_info_,
                                     emu_t::ptr16_t& executable_buffer_ )
    {
        uint8_t* executable_buffer = e.byte_ptr( executable_buffer_ );

        const std::string filename( exec_info_->filename.data() );
        const std::string game_dir = R"(F:\projects\fun\dos_games_rev\alpha_waves_dev\tests\alpha)";
        const std::string file_path = game_dir + "\\" + filename;

        const std::vector<uint8_t> prog_cc1_content = read_binary_file( file_path );
        assert( prog_cc1_content.size() == sizeof( progs_cc1_t ) );
        const progs_cc1_t* progs_cc1 = reinterpret_cast<const progs_cc1_t*>( prog_cc1_content.data() );

        struct exec_data_slice_t
        {
            uint32_t data_size{};
            uint32_t unpacked_data_size{};
            const uint8_t* data{};
        };

        auto get_slice = []( const auto& exec_data_ ) {
            return exec_data_slice_t{ swap( exec_data_.data_size ), swap( exec_data_.unpacked_data_size ),
                                      exec_data_.data.data() };
        };

        const exec_data_slice_t slice = [&]( size_t exec_type_ ) {
            switch( exec_info_->byte_12h )
            {
                case 0:
                    return get_slice( progs_cc1->sound.pc_buz );
                case 1:
                    return get_slice( progs_cc1->sound.tandy );
                case 2:
                    return get_slice( progs_cc1->sound.adlib );
                case 3:
                    return get_slice( progs_cc1->gfx.cga_hercules );
                case 4:
                    return get_slice( progs_cc1->gfx.tandy );
                case 5:
                    return get_slice( progs_cc1->gfx.ega_vga );
                default:
                    assert( false );
                    break;
            }
            return exec_data_slice_t{};
        }( exec_info_->byte_12h );

        std::vector<uint8_t> compress_data( slice.data, slice.data + slice.data_size );
        std::vector<uint8_t> uncompressed_data( slice.unpacked_data_size );

        // some sort of uncompression, after that the executable is at executable_buffer[0] with size = slice.unpacked_data_size
        emu_GAME_START_sub_3( uncompressed_data, compress_data );

        // only for the original port comparison
        ::memcpy( e.memory( executable_buffer_ ), uncompressed_data.data(), uncompressed_data.size() );

        printf( "---------\n" );

        return slice.unpacked_data_size;
    }

} // namespace cleanup
