-- DESCRIPTION: Verilator: a final else in a procedure's if statement is unconditional
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    n     : in  integer;
    o_pos : out integer;
    o_neg : out integer
  );
end entity t;

architecture rtl of t is
  -- The if is followed by another statement, so the body is lowered as a chain and the
  -- statements after the if are appended to every branch: the final else has no condition
  -- of its own to become an if with
  procedure pick(v : in integer; res : out integer) is
    variable acc : integer := 0;
  begin
    if v > 0 then
      acc := 1;
    else
      acc := 2;
    end if;
    res := acc + 10;
  end procedure pick;
begin

  pos_proc : process (n)
    variable r_pos : integer := 0;
  begin
    pick(n, r_pos);
    o_pos <= r_pos;
  end process pos_proc;

  neg_proc : process (n)
    variable r_neg : integer := 0;
  begin
    pick(n - 4, r_neg);
    o_neg <= r_neg;
  end process neg_proc;

end architecture rtl;
