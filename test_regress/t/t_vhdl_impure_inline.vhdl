-- DESCRIPTION: Verilator: an impure function that reads no file inlines at elaboration
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- poc's mem_InitMemory is impure because it may read a memory image file, but with no
-- file name it returns a constant image. An impure function whose body reads no file is
-- inlined like a pure one; the frontend still rejects a signal or file read elsewhere.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package imp is
  impure function scale(v : integer) return integer;

  constant scaled_c : integer := scale(3);
end package;

package body imp is
  impure function scale(v : integer) return integer is
  begin
    return v * 7;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.imp.all;

entity t is
  port (
    clk : in  std_logic;
    o   : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of t is
begin
  process (clk)
  begin
    if rising_edge(clk) then
      o <= std_logic_vector(to_unsigned(scaled_c, 8));
    end if;
  end process;
end architecture;
