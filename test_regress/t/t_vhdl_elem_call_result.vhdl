-- DESCRIPTION: Verilator: one element of an array a function returns with an open element
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- `T_SLV` is an array whose element is itself unconstrained. An element of a returned
-- T_SLV takes its width from the local the body returns.

library ieee;
use ieee.std_logic_1164.all;

package openelem is
  type t_slv is array (natural range <>) of std_logic_vector;
  constant c : t_slv(1 downto 0)(3 downto 0) := ("1011", "0100");
  function row (x : t_slv) return t_slv;
end package;

package body openelem is
  function row (x : t_slv) return t_slv is
    variable r : t_slv(x'range)(x(0)'range);
  begin
    r := x;
    return r;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.openelem.all;

entity t is
  port (
    o : out std_logic
  );
end entity;

architecture rtl of t is
begin
  o <= row(c)(1)(3);
end architecture;
