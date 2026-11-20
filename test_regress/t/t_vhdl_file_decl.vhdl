-- DESCRIPTION: Verilator: VHDL file declarations are accepted wherever they are legal
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
library std;
use std.textio.all;

package file_pkg is
  file spec_file : text;
  impure function bump(v : integer) return integer;
end package file_pkg;

package body file_pkg is
  -- A file declaration in a subprogram body, which makes the subprogram impure
  impure function bump(v : integer) return integer is
    file body_file : text;
  begin
    return v + 1;
  end function bump;
end package body file_pkg;

library ieee;
use ieee.std_logic_1164.all;
library std;
use std.textio.all;

library work;
use work.file_pkg.all;

entity t is
  port (
    clk : in  std_logic;
    o   : out integer;
    p   : out std_logic
  );
  -- A file declaration in an entity declarative part
  file ent_file : text;
end entity t;

architecture rtl of t is
  file arch_file : text;
begin

  -- A block declarative part, with nothing but a file in it
  blk : block
    file blk_file : text;
  begin
    p <= '1';
  end block blk;

  -- A process declarative part, beside a process variable
  proc : process (clk)
    file proc_file : text;
    variable acc : integer := 0;
  begin
    if rising_edge(clk) then
      acc := 5;
      o   <= acc;
    end if;
  end process proc;

end architecture rtl;