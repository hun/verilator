-- DESCRIPTION: Verilator: a deferred type used by a later initializer
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

entity t is
  generic (N : natural := 8);
  port (q : out unsigned(2 downto 0));
end entity t;

architecture rtl of t is
  -- Real-valued, so the constant is only known once it is emitted
  constant ADDR_BITS : integer := integer(ceil(log2(real(N))));
  function twice return integer is
  begin
    return ADDR_BITS * 2;
  end function twice;
  constant DATA_BITS : integer := twice;
  type state_t is record
    count : integer range 0 to N + 1;
    addr  : unsigned(ADDR_BITS - 1 downto 0);
    data  : unsigned(DATA_BITS - 1 downto 0);
  end record;
  function null_state return state_t is
    variable ret : state_t;
  begin
    ret.count := 0;
    ret.addr := to_unsigned(5, ADDR_BITS);
    ret.data := to_unsigned(0, DATA_BITS);
    return ret;
  end function null_state;
  signal st : state_t := null_state;
begin
  q <= st.addr;
end architecture rtl;
