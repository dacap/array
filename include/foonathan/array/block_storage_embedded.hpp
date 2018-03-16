// Copyright (C) 2018 Jonathan Müller <jonathanmueller.dev@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level directory of this distribution.

#ifndef FOONATHAN_ARRAY_BLOCK_STORAGE_EMBEDDED_HPP_INCLUDED
#define FOONATHAN_ARRAY_BLOCK_STORAGE_EMBEDDED_HPP_INCLUDED

#include <algorithm>
#include <exception>

#include <foonathan/array/block_storage.hpp>

namespace foonathan
{
    namespace array
    {
        /// Exception thrown when [array::block_storage_embedded]() is exhausted.
        class embedded_storage_overflow : public std::exception
        {
        public:
            const char* what() const noexcept override
            {
                return "overflow of an embedded storage";
            }
        };

        /// A `BlockStorage` that stores a block up to `BufferBytes` big directly inside.
        template <std::size_t BufferBytes>
        class block_storage_embedded
        {
        public:
            using embedded_storage = std::true_type;
            using arg_type         = block_storage_args_t<>;

            //=== constructor/destructor ===//
            explicit block_storage_embedded(arg_type) noexcept {}

            block_storage_embedded(const block_storage_embedded&) = delete;
            block_storage_embedded& operator=(const block_storage_embedded&) = delete;

            ~block_storage_embedded() noexcept = default;

            template <typename T>
            static void swap(
                block_storage_embedded& lhs, block_view<T>& lhs_constructed,
                block_storage_embedded& rhs,
                block_view<T>&
                    rhs_constructed) noexcept(std::is_nothrow_move_constructible<T>::value)
            {
                // move both to front to simplify swap logic
                move_to_front(lhs, lhs_constructed);
                move_to_front(rhs, rhs_constructed);

                // swap the common prefix over
                auto lhs_size = lhs_constructed.size();
                auto rhs_size = rhs_constructed.size();
                auto min_size = std::min(lhs_size, rhs_size);
                std::swap_ranges(lhs_constructed.begin(), lhs_constructed.begin() + min_size,
                                 rhs_constructed);

                // now move the remaining elements over
                if (lhs_size > rhs_size)
                    uninitialized_destructive_move(lhs_constructed.begin() + min_size,
                                                   lhs_constructed.end(),
                                                   memory_block(rhs.block().begin() + min_size,
                                                                rhs.block().end()));
                else
                    uninitialized_destructive_move(rhs_constructed.begin() + min_size,
                                                   rhs_constructed.end(),
                                                   memory_block(lhs.block().begin() + min_size,
                                                                lhs.block().end()));

                lhs_constructed = block_view<T>(memory_block(lhs.block().begin(), rhs_size));
                rhs_constructed = block_view<T>(memory_block(rhs.block().begin(), lhs_size));
            }

            //=== reserve/shrink_to_fit ===//
            template <typename T>
            raw_pointer reserve(size_type min_additional_bytes, const block_view<T>& constructed)
            {
                // move to front to allow maximal size
                auto new_end = as_raw_pointer(move_to_front(*this, constructed).data_end());

                // check for overflow
                auto new_size = block().size() + min_additional_bytes;
                if (new_size <= max_size())
                    return new_end;
                else
                    throw embedded_storage_overflow();
            }

            template <typename T>
            raw_pointer shrink_to_fit(const block_view<T>& constructed) noexcept(
                std::is_nothrow_move_constructible<T>{})
            {
                // we move it to the front for good measure
                return as_raw_pointer(move_to_front(*this, constructed).data_end());
            }

            //=== accessors ===//
            memory_block block() const noexcept
            {
                return memory_block(as_raw_pointer(&storage_), BufferBytes);
            }

            arg_type arguments() const noexcept
            {
                return {};
            }

            static size_type max_size() noexcept
            {
                return BufferBytes;
            }

        private:
            using storage = typename std::aligned_storage<BufferBytes>::type;
            storage storage_;
        };
    }
} // namespace foonathan::array

#endif // FOONATHAN_ARRAY_BLOCK_STORAGE_EMBEDDED_HPP_INCLUDED
