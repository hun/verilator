-- DESCRIPTION: Verilator: 'range of a function call
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (len   : out integer;
        low   : out integer;
        high  : out integer;
        count : out integer);
end entity t;

architecture rtl of t is
  type range_t is array (integer range <>) of bit;

  function get_range (constant code : string) return range_t is
    constant ret_val : range_t(3 to 12) := (others => '0');
  begin
    return ret_val;
  end function get_range;

  -- The call, not the function's declared type, fixes the bounds
  signal flags : boolean_vector(get_range("HELLO")'range) := (others => false);
begin
  len   <= get_range("HELLO")'length;
  low   <= get_range("HELLO")'left;
  high  <= get_range("HELLO")'right;
  count <= flags'length;
end architecture rtl;
