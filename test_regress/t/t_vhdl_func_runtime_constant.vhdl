-- DESCRIPTION: Verilator: a function constant initialized from a formal
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package p is
  function swap_bytes (inp : in std_logic_vector) return std_logic_vector;
end package p;

package body p is
  function swap_bytes (inp : in std_logic_vector) return std_logic_vector is
    -- Neither the value nor the range is known until the call
    constant Inp_c   : std_logic_vector(inp'length-1 downto 0) := inp;
    constant Bytes_c : natural := inp'length/8;
    variable Result_v : std_logic_vector(Inp_c'range);
  begin
    for i in 0 to Bytes_c-1 loop
      Result_v(8*i+7 downto 8*i) := Inp_c(8*(Bytes_c-1-i)+7 downto 8*(Bytes_c-1-i));
    end loop;
    return Result_v;
  end function swap_bytes;
end package body p;

library ieee;
use ieee.std_logic_1164.all;
use work.p.all;

entity t is
  port (d : in std_logic_vector(15 downto 0);
        q : out std_logic_vector(15 downto 0));
end entity t;

architecture rtl of t is
begin
  q <= swap_bytes(d);
end architecture rtl;
